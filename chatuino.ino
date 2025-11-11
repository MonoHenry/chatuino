// index_postgres.js - Script Node.js para leitura Serial e PostgreSQL

const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline'); 
const { Client } = require('pg'); 
require('dotenv').config(); 

// ----------------------------------------------------
// Configurações - AJUSTE AQUI
const PORT_NAME = "/dev/ttyUSB0";    
const BAUD_RATE = 115200;            

// 💡 NOVO: Expressão Regular para validar e extrair o formato "XX: <mensagem>"
// ^(\d{2}):\s*(.*)$
// Grupo 1: (\d{2}) -> Captura o ID de dois dígitos (00-99)
// Grupo 2: (.*) -> Captura o restante da mensagem
// ----------------------------------------------------

// Variáveis globais para a conexão
let client;

// --- FUNÇÕES DE BANCO DE DADOS ---

/**
 * @brief Configura e conecta ao banco de dados PostgreSQL (Neon), criando a tabela.
 * @returns {Promise<Client>} A conexão aberta com o banco de dados.
 */
async function setupDatabase() {
    const connectionString = process.env.DATABASE_URL;
    
    if (!connectionString) {
        throw new Error("❌ Variável de ambiente DATABASE_URL não definida.");
    }

    client = new Client({
        connectionString: connectionString,
    });

    await client.connect();

    // Tabela inalterada
    await client.query(`
        CREATE TABLE IF NOT EXISTS leituras_brutas (
            id SERIAL PRIMARY KEY,
            mensagem_bruta TEXT NOT NULL,
            id_user INTEGER NOT NULL,
            timestamp TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP
        );
    `);

    console.log(`✅ Conectado ao PostgreSQL (Neon) e tabela 'leituras_brutas' pronta.`);
    return client;
}

// --- FUNÇÕES DE MONITORAMENTO SERIAL ---

/**
 * @brief Inicializa a porta serial e o loop de leitura.
 * @param {Client} dbClient - O cliente de conexão com o banco de dados PostgreSQL.
 */
function startSerialMonitor(dbClient) {
    // Expressão regular compilada para uso eficiente
    const DATA_REGEX = /^(\d{2}):\s*(.*)$/; 

    try {
        const port = new SerialPort({ 
            path: PORT_NAME, 
            baudRate: BAUD_RATE, 
        });

        const parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));
        
        port.on('open', () => {
            console.log(`✅ Porta serial '${PORT_NAME}' aberta. Monitorando (Formato: XX: Mensagem)...`);
        });

        port.on('error', (err) => {
            console.error("❌ Erro na porta serial:", err.message);
        });

        // Loop de leitura e inserção
        parser.on('data', async (line) => {
            const trimmedLine = line.trim();

            if (trimmedLine.length > 0) {
                
                // 💡 NOVO: Validação e Extração
                const match = trimmedLine.match(DATA_REGEX);

                if (!match) {
                    console.log(`⚠️ IGNORADO (Formato Inválido): ${trimmedLine}`);
                    return; // Ignora mensagens que não seguem o padrão "XX: <mensagem>"
                }

                // Extrai os grupos da Expressão Regular
                const id_user_str = match[1]; // Grupo 1: o ID de dois dígitos (string)
                const mensagem_bruta = match[2].trim(); // Grupo 2: a mensagem, removendo espaços extras
                
                const id_user_int = parseInt(id_user_str, 10);

                if (mensagem_bruta.length === 0) {
                     console.log(`⚠️ IGNORADO (Mensagem Vazia): ${trimmedLine}`);
                     return;
                }
                
                try {
                    // SQL e Parâmetros atualizados
                    const sql = "INSERT INTO leituras_brutas (mensagem_bruta, id_user) VALUES ($1, $2) RETURNING id";
                    const values = [mensagem_bruta, id_user_int]; 

                    const result = await dbClient.query(sql, values);
                    
                    console.log(`💾 SALVO (ID: ${result.rows[0].id} | USER: ${id_user_int}): ${mensagem_bruta}`);
                } catch (e) {
                    console.error("❌ Erro ao inserir no BD:", e.message);
                }
            }
        });

    } catch (e) {
        console.error("❌ Erro ao inicializar a porta serial:", e.message);
    }
}

// --- FUNÇÃO PRINCIPAL ---

async function main() {
    try {
        const dbClient = await setupDatabase();
        startSerialMonitor(dbClient);
    } catch (e) {
        console.error("❌ Falha crítica no MAIN:", e.message);
        if (client) {
            console.log("Fechando conexão com o banco de dados...");
            await client.end(); 
        }
    }
}

main();
