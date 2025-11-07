#include <ModbusMaster.h>
#include <HardwareSerial.h>

#define RS485_TX_PIN 17
#define RS485_RX_PIN 16
#define RS485_DE_PIN 4
#define RS485_RE_PIN 4

ModbusMaster node;

void preTransmission() {
  digitalWrite(RS485_DE_PIN, HIGH);  // Send mode
}

void postTransmission() {
  digitalWrite(RS485_DE_PIN, LOW);   // Receive mode
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);

  pinMode(RS485_DE_PIN, OUTPUT);
  digitalWrite(RS485_DE_PIN, LOW);  // Start in receive mode

  Serial.println("Starting Modbus slave scan...");

  node.begin(1, Serial2);  // Initial ID (will be changed in loop)
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);
}

void loop() {
  for (uint8_t id = 1; id <= 247; id++) {
    node.begin(id, Serial2);  // Set slave ID

    // Prøv at læse 1 input register ved adresse 0
    uint8_t result = node.readInputRegisters(1, 1);
    if (result == node.ku8MBSuccess) {
      uint16_t value = node.getResponseBuffer(0);
      Serial.print("Found slave ID: ");
      Serial.print(id);
      Serial.print(" | Register 0 value: ");
      Serial.println(value);
      while (1); // Stop scanning når vi finder den
    } else {
      Serial.print("No response from slave ID: ");
      Serial.println(id);
    }

    delay(100); // Kort pause mellem ID'er
  }

  Serial.println("Scan complete. No slave found.");
  delay(5000);  // Vent før næste scan
}
