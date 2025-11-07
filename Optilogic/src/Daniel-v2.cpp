#include <ModbusMaster.h>
#include <HardwareSerial.h>

// ===================== RS485 PINS =====================
#define RS485_TX_PIN 17    // UART2 TX
#define RS485_RX_PIN 16    // UART2 RX
#define RS485_DE_PIN 4     // DE + RE (send/modtag)
#define GreenPin 3         // Status LED

// ===================== MODBUS =====================
#define MODBUS_SLAVE_ID 1  // Slave ID ventilator
#define BAUD_RATE 9600     // Baudrate

ModbusMaster node;

void preTransmission() {
  digitalWrite(RS485_DE_PIN, HIGH);  // Sæt til send
}

void postTransmission() {
  digitalWrite(RS485_DE_PIN, LOW);   // Sæt til modtag
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(BAUD_RATE, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);

  pinMode(RS485_DE_PIN, OUTPUT);
  digitalWrite(RS485_DE_PIN, LOW);  // Start i modtag-tilstand

  pinMode(GreenPin, OUTPUT);

  // Start Modbus
  node.begin(MODBUS_SLAVE_ID, Serial2);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  Serial.println("Modbus RTU master setup complete");
}

void loop() {
  uint8_t result;

  // ====== 1. Skriv register 3 til 5 (Normal run) ======
  result = node.writeSingleRegister(3, 5);  // 3 = Operating mode
  if (result == node.ku8MBSuccess) {
    Serial.println("Ventilator tændt (Normal run)");
    digitalWrite(GreenPin, HIGH);
  } else {
    Serial.print("Modbus write error: ");
    Serial.println(result);
    digitalWrite(GreenPin, LOW);
  }

  delay(500); // Lille pause før læsning

  // ====== 2. Læs register 3 for at bekræfte ======
  result = node.readHoldingRegisters(3, 1);
  if (result == node.ku8MBSuccess) {
    uint16_t value = node.getResponseBuffer(0);
    Serial.print("Register 3 value: ");
    Serial.println(value);
  } else {
    Serial.print("Modbus read error: ");
    Serial.println(result);
  }

  Serial.println("----------------------------");
  delay(1000); // Gentag hvert 1 sekund
}
