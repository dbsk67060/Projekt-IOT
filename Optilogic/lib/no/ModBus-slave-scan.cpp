#include <ModbusMaster.h>
#include <HardwareSerial.h>

// ======= RS485 PINS =======
#define RS485_TX_PIN 4    // ESP32 TX → RS485 DI
#define RS485_RX_PIN 36   // ESP32 RX ← RS485 RO
#define RS485_DIR 22      // DE/RE styring (send/modtag)

// ======= Modbus object =======
ModbusMaster node;

void preTransmission() { 
  digitalWrite(RS485_DIR, HIGH);  // Send mode
}

void postTransmission() { 
  digitalWrite(RS485_DIR, LOW);   // Receive mode
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);

  pinMode(RS485_DIR, OUTPUT);
  digitalWrite(RS485_DIR, LOW);  // Start i modtag-tilstand

  // Initial slave setup (ID skiftes i loop)
  node.begin(1, Serial2);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  Serial.println("Starting fast Modbus slave scan...");
}

void loop() {
  for (uint8_t id = 1; id <= 247; id++) {
    node.begin(id, Serial2);  // Skift slave ID

    // Læs register 3 (typisk "Operating mode" på ventilatorer)
    uint8_t result = node.readHoldingRegisters(3, 1);

    if (result == node.ku8MBSuccess) {
      uint16_t value = node.getResponseBuffer(0);
      Serial.println("======================================");
      Serial.print("✅ Found slave ID: ");
      Serial.println(id);
      Serial.print("Register 3 value: ");
      Serial.println(value);
      Serial.println("======================================");
      while (1); // Stop scanning, vi har fundet slave
    } else {
      Serial.print("No response from slave ID: ");
      Serial.println(id);
    }

    delay(50); // Kort pause mellem ID'er for stabilitet
  }

  Serial.println("Scan complete. No slave found.");
  while(1); // Stop hvis ingen slave reagerer
}
