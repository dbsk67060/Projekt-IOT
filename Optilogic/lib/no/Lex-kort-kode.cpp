#include <ModbusRTU.h>

ModbusRTU mb;

#define DE_RE 4     // RS485 driver enable-pin
#define RX_PIN 16
#define TX_PIN 17

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);  // match PLC baudrate
  mb.begin(&Serial2, DE_RE);
  mb.master();

  delay(2000); // lidt ventetid før første send
  Serial.println("Sender start-signal (coil 84 = ON)");

  // Skriv coil 84 = true (start)
  mb.writeCoil(1, 81, true);   // (slave ID 1, adresse 84, værdi ON)
}

void loop() {
  mb.task(); // holder Modbus aktiv
}