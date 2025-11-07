#include <Arduino.h>
#include <ModbusRTU.h>

#define SLAVE_ID 1
#define RS485_SERIAL_PORT Serial2
#define RS485_DE_RE_PIN 4

ModbusRTU mb;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n--- Modbus RTU test start ---");

  // Start RS485 (9600 baud, 8N1, RX=16, TX=17)
  RS485_SERIAL_PORT.begin(9600, SERIAL_8N1, 36, 4);

  // Konfigurer RS485 retningspin
  pinMode(RS485_DE_RE_PIN, OUTPUT);
  digitalWrite(RS485_DE_RE_PIN, LOW);

  // Initialiser Modbus som master
  mb.begin(&RS485_SERIAL_PORT, RS485_DE_RE_PIN);
  mb.master();

  Serial.println("ModbusRTU initialiseret. Tester forbindelse...");
}

bool cb(Modbus::ResultCode event, uint16_t transactionId, void* data) {
  Serial.printf("Callback resultat: 0x%02X\n", event);
  if (event == Modbus::EX_SUCCESS) {
    Serial.println("Slave svarede korrekt!");
  } else {
    Serial.println("Ingen svar / fejl i kommunikation.");
  }
  return true;
}

void loop() {
  static uint32_t lastPing = 0;
  static uint16_t value;

  if (millis() - lastPing > 3000) {
    lastPing = millis();
    Serial.println("\nSender Modbus forespørgsel...");
    // Læs holding register 0 fra slave 1
    if (!mb.readHreg(SLAVE_ID, 0, &value, 1, cb)) {
      Serial.println("Kunne ikke sende Modbus-forespørgsel!");
    }
  }

  mb.task();
  yield();
}
