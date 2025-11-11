const { Client } = require('pg'); // Cliente PostgreSQL
const dotenv = require('dotenv'); // Para carregar variáveis de ambiente de um arquivo .env
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');
const express = require('express'); // Adicionamos o Express
const path = require('path');     // Adicionamos o Path

// Carrega as variáveis de ambiente do .env
dotenv.config();

// ----------------------------------------------------
// Configurações - AJUSTE AQUI
const PORT_NAME = "/dev/ttyUSB0";   // Substitua pela sua porta (Ex: "COM3" no Windows)
const BAUD_RATE = 115200;           // Deve ser igual ao Serial.begin() do Arduino
const NEON_DATABASE_URL = process.env.DATABASE_URL; // URL de conexão do Neon
const HTTP_PORT = process.env.PORT || 3000; // Porta para o servidor web
// ----------------------------------------------------

// Expressão regular para validar o formato "XX: <mensagem>"
const MESSAGE_REGEX = /^(\d{2}):\s*(.+)$/;

// Variável global para o cliente do banco de dados
let dbClient;

/**
 * @brief Configura e conecta ao banco de dados PostgreSQL (Neon).
 * @returns {Promise<Client>} A conexão aberta com o banco de dados.
 */
async function setupDatabase() {
    if (!NEON_DATABASE_URL) {
        throw new Error("❌ DATABASE_URL não encontrada. Crie um arquivo .env e adicione a URL de conexão do Neon.");
    }

    const client = new Client({
        connectionString: NEON_DATABASE_URL,
        // Adicione SSL se o Neon exigir (geralmente sim para conexões externas)
        // ssl: {
        //   rejectUnauthorized: false
        // }
    });

    await client.connect();

    // Cria a tabela, adaptada para PostgreSQL.
    await client.query(`
        CREATE TABLE IF NOT EXISTS leituras_brutas (
            id SERIAL PRIMARY KEY,
            identificador VARCHAR(2) NOT NULL,
            mensagem_processada TEXT NOT NULL,
            timestamp TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP
        );
    `);

    console.log(`✅ Conectado ao BD PostgreSQL (Neon) e tabela 'leituras_brutas' pronta.`);
    return client;
}

/**
 * @brief Inicializa a porta serial e o loop de leitura.
 * @param {Client} db - A conexão com o banco de dados PostgreSQL.
 */
function startSerialMonitor(db) {
    try {
        const port = new SerialPort({
            path: PORT_NAME,
            baudRate: BAUD_RATE,
        });

        const parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));

        port.on('open', () => {
            console.log(`✅ Porta serial '${PORT_NAME}' aberta. Monitorando e salvando mensagens...`);
        });

        port.on('error', (err) => {
            console.error("❌ Erro na porta serial:", err.message);
        });

        parser.on('data', async (line) => {
            const trimmedLine = line.trim();
            console.log(`Recebido: ${trimmedLine}`);

            const match = trimmedLine.match(MESSAGE_REGEX);

            if (match) {
                const identificador = match[1];
                const mensagem = match[2].trim();

                try {
                    await db.query(
                        "INSERT INTO leituras_brutas (identificador, mensagem_processada) VALUES ($1, $2)",
                        [identificador, mensagem]
                    );
                    console.log(`💾 SALVO: ID=${identificador}, Mensagem='${mensagem}'`);
                } catch (e) {
                    console.error("❌ Erro ao inserir no BD:", e.message);
                }
            } else if (trimmedLine.length > 0) {
                console.log(`⚠️ IGNORADO: Formato inválido. Esperado "XX: <mensagem>". Recebido: "${trimmedLine}"`);
            }
        });

    } catch (e) {
        console.error("❌ Erro ao inicializar a porta serial:", e.message);
    }
}

/**
 * @brief Configura e inicia o servidor web Express.
 */
function startWebServer() {
    const app = express();
    
    // Rota principal que serve o arquivo index.html
    app.get('/', (req, res) => {
        res.sendFile(path.join(__dirname, 'index.html'));
    });

    // Rota de API para buscar as mensagens
    app.get('/api/messages', async (req, res) => {
        if (!dbClient) {
            return res.status(503).json({ error: 'Banco de dados não conectado.' });
        }
        
        try {
            // Busca as últimas 50 mensagens, das mais antigas para as mais novas
            const { rows } = await dbClient.query(`
                SELECT * FROM (
                    SELECT identificador, mensagem_processada, timestamp 
                    FROM leituras_brutas 
                    ORDER BY timestamp DESC 
                    LIMIT 50
                ) sub
                ORDER BY timestamp ASC;
            `);
            res.json(rows);
        } catch (e) {
            console.error("❌ Erro ao buscar mensagens:", e.message);
            res.status(500).json({ error: 'Erro ao consultar o banco de dados.' });
        }
    });

    app.listen(HTTP_PORT, () => {
        console.log(`🚀 Servidor web rodando em http://localhost:${HTTP_PORT}`);
    });
}

/**
 * Função principal assíncrona
 */
async function main() {
    try {
        dbClient = await setupDatabase(); // Armazena o cliente na variável global
        startSerialMonitor(dbClient);     // Inicia o monitor serial (seu código original)
        startWebServer();                 // Inicia o servidor web (novo)
    } catch (e) {
        console.error("❌ Falha crítica na inicialização:", e.message);
        if (dbClient) {
            await dbClient.end();
        }
        process.exit(1); // Encerra o processo em caso de falha crítica
    }
}

main();
