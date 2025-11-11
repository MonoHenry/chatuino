#include <SPI.h>
#include "printf.h"
#include "RF24.h"

#define CE_PIN 7
#define CSN_PIN 8

// TYPES
#define DATA 0
#define ACK 1
#define RTS 2
#define CTS 3

#define MAX_SIZE 32

#define TIMEOUT 500  //millis
RF24 radio(CE_PIN, CSN_PIN);

uint64_t address[2] = { 0x4040404040LL, 0x3030303030LL };
uint8_t origem = 47;
uint8_t destino = 26;
uint8_t servidor;
uint8_t payload[MAX_SIZE];
uint8_t buffer[MAX_SIZE];


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  while (!Serial) {
    // some boards need to wait to ensure access to serial over USB
  }

  // initialize the transceiver on the SPI bus
  if (!radio.begin()) {
    Serial.println(F("radio hardware is not responding!!"));
    while (1) {}  // hold in infinite loop
  }

  radio.setPALevel(RF24_PA_MAX);  // RF24_PA_MAX is default.
  radio.setChannel(121);
  radio.setPayloadSize(sizeof(payload));  // float datatype occupies 4 bytes
  radio.setAutoAck(false);
  radio.setCRCLength(RF24_CRC_DISABLED);
  radio.setDataRate(RF24_250KBPS);

  radio.openWritingPipe(address[0]);     // always uses pipe 0
  radio.openReadingPipe(1, address[1]);  // using pipe 1

  //For debugging info
  printf_begin();  // needed only once for printing details
  //radio.printDetails();       // (smaller) function that prints raw register values
  radio.printPrettyDetails();  // (larger) function that prints human readable data
}

void printPacote(byte* pac, int tamanho) {
  Serial.print(F("Rcvd "));
  Serial.print(tamanho);  // print the size of the payload
  Serial.print(F(" O: "));
  Serial.print(pac[0]);  // print the payload's value
  Serial.print(F(" D: "));
  Serial.print(pac[1]);  // print the payload's value
  Serial.print(F(" C: "));
  Serial.print(pac[2]);  // print the payload's value
  Serial.print(F(" i: "));
  Serial.print(pac[3]);  // print the payload's value
  Serial.print(F(" : "));
  for (int i = 4; i < tamanho; i++) {
    Serial.print(pac[i]);
  }
  Serial.println();  // print the payload's value
}

uint8_t checksum_f(uint8_t* mensagem, int size) {
  uint16_t sum = 0;
  for (int i = 0; i < size; i++)
    sum += mensagem[i];

  return (uint8_t)(sum & 0xFF);
}

void envia(int destino, int tipo, uint8_t* mensagem, uint8_t size) {
  radio.flush_tx();

  payload[0] = origem;
  payload[1] = destino;
  payload[2] = tipo;
  payload[3] = size + 1;

  for (int i = 0; i < size - 4; i++) {
    payload[i + 4] = mensagem[i];
  }

  uint8_t checksum = checksum_f(payload, size);
  payload[size] = checksum;

  unsigned long inicio = millis();
  while (millis() - inicio < TIMEOUT) {
    radio.startListening();
    delayMicroseconds(130);
    if (!radio.testCarrier()) {
      radio.stopListening();
      radio.write(&payload[0], size + 1);
      // for(int i = 0; i < size + 1; i++)
      //   Serial.print(payload[i]);
      // Serial.println("");
      return;
    } else {
      Serial.println("Meio Ocupado");
      delayMicroseconds(270);
    }
    radio.flush_tx();
  }
  Serial.println("TimeOut!");
}

int envia_pacote(int destino, byte* mensagem, uint8_t size) {
  if (size + 5 > MAX_SIZE)
    return 1;

  envia(destino, RTS, "", 4);

  if (!recebe(CTS, destino)) {
    uint8_t add = buffer[0];
    // if (add == destino)
    //   Serial.println("CTS Recebido!");
  } else {
    // Serial.println("CTS não recebido!");
    return 2;
  }

  envia(destino, DATA, mensagem, size + 4);

  if (!recebe(ACK, destino)) {
    uint8_t add = buffer[0];
    // if (add == destino)
    //   Serial.println("ACK Recebido!");
  } else {
    // Serial.println("ACK não recebido!");
    return 3;
  }
  return 0;
}

int recebe(int type, int destino) {
  int tamanho;
  radio.startListening();
  unsigned long inicio = millis();
  while (millis() - inicio < TIMEOUT) {
    if (radio.available()) {
      delay(10);
      radio.read(buffer, MAX_SIZE);
      tamanho = buffer[3];
      for(int i = 0; i < tamanho; i++)
        Serial.print(buffer[i]);
      Serial.println("");
      if (buffer[0] != destino)
        continue;

      if (buffer[1] != origem)
        continue;

      if (buffer[2] != type)
        continue;

      if (tamanho > MAX_SIZE)
        continue;

      uint8_t checksum = checksum_f(buffer, tamanho - 1);
      if (buffer[tamanho - 1] != checksum) {
        continue;
      }

      
      if (type == DATA) {
        Serial.print(buffer[0]);
        Serial.print(": ");
        for(int i = 4; i < tamanho-1; i++) {
          Serial.print((char)buffer[i]);
        }
        Serial.println("");
      }

      radio.flush_rx();
      return 0;
    }
  }
  Serial.print(".");
  return 1;
}

int escutar(uint8_t destino) {
  if(!recebe(RTS, destino)){
    envia(destino, CTS, "", 4);
  }

  if(!recebe(DATA, destino)){
    envia(destino, ACK, "", 4);
  }
}

void loop() {
  if(Serial.available()){
    String message = Serial.readStringUntil('\n');
    message.trim();
    
    if(message.length() > 0){
      Serial.println("Enviando mensagem...");
      Serial.print("Você: ");
      Serial.print(message);
      Serial.println("");

      uint8_t msg[message.length()];
      for(int i = 0; i < message.length(); i++){
        msg[i] = message[i];
      }

      if(envia_pacote(destino, msg, message.length()) == 0){
        Serial.println("Mensagem Enviada!");
      } else {
        Serial.println("Erro no envio.");
      }
    }
  }

  escutar(26);
  
  delay(10);
}
