
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline'); 
const sqlite3 = require('sqlite');
const driver = require('sqlite3'); // Driver necessário para o pacote 'sqlite'

// ----------------------------------------------------
// Configurações - AJUSTE AQUI
const PORT_NAME = "/dev/ttyUSB0";    // Substitua pela sua porta (Ex: "COM3" no Windows)
const BAUD_RATE = 115200;            // Deve ser igual ao Serial.begin() do Arduino
const DB_NAME = "arduino_buffer.db";
// ----------------------------------------------------

/**
 * @brief Configura e conecta ao banco de dados SQLite.
 * @returns {Promise<sqlite3.Database>} A conexão aberta com o banco de dados.
 */
async function setupDatabase() {
    const db = await sqlite3.open({
        filename: DB_NAME,
        driver: driver.Database
    });

    await db.exec(`
        CREATE TABLE IF NOT EXISTS leituras_brutas (
            id INTEGER PRIMARY KEY,
            mensagem_bruta TEXT NOT NULL,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    `);

    console.log(`✅ Conectado ao BD e tabela 'leituras_brutas' pronta: ${DB_NAME}`);
    return db;
}

/**
 * @brief Inicializa a porta serial e o loop de leitura.
 * @param {sqlite3.Database} db - A conexão com o banco de dados.
 */
function startSerialMonitor(db) {
    try {
        // 1. Configura e abre a porta serial
        const port = new SerialPort({ 
            path: PORT_NAME, 
            baudRate: BAUD_RATE, 
        });

        // 2. Configura o parser para ler linhas (delimitadas por '\n')
        const parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));
        
        // 3. Monitoramento de eventos
        port.on('open', () => {
            console.log(`✅ Porta serial '${PORT_NAME}' aberta. Monitorando e salvando mensagens...`);
        });

        port.on('error', (err) => {
            console.error("❌ Erro na porta serial:", err.message);
        });

        // 4. Loop de leitura e inserção
        parser.on('data', async (line) => {
            const trimmedLine = line.trim();

            if (trimmedLine.length > 0) {
                try {
                    // Insere a mensagem bruta no banco de dados
                    await db.run(
                        "INSERT INTO leituras_brutas (mensagem_bruta) VALUES (?)",
                        [trimmedLine]
                    );
                    console.log(`💾 SALVO: ${trimmedLine}`);
                } catch (e) {
                    console.error("❌ Erro ao inserir no BD:", e.message);
                }
            }
        });

    } catch (e) {
        console.error("❌ Erro ao inicializar a porta serial:", e.message);
    }
}

/**
 * Função principal assíncrona
 */
async function main() {
    const db = await setupDatabase();
    startSerialMonitor(db);
}

main().catch(console.error);
