#include <Arduino.h>
#include <ArduinoModbus.h>

#define RXD2 16
#define TXD2 17

void setup() {
  Serial.begin(115200);
  Serial.println("Booting ESP32 Modbus RTU server...");

  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  if (!ModbusRTUServer.begin(1, Serial2)) {
    Serial.println("Failed to start Modbus RTU Server!");
    while (1);
  }
  Serial.println("Modbus RTU Server started successfully.");

  // 5 holding registers (0–4)
  ModbusRTUServer.configureHoldingRegisters(0, 5);
  ModbusRTUServer.holdingRegisterWrite(0, 245);    // temp10
  ModbusRTUServer.holdingRegisterWrite(1, 450);    // hum10
  ModbusRTUServer.holdingRegisterWrite(2, 10125);  // press10
  ModbusRTUServer.holdingRegisterWrite(3, 1200);   // rpm
  ModbusRTUServer.holdingRegisterWrite(4, 1);      // status
}

void loop() {
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 1000) {
    Serial.print("Holding registers: ");
    for (int i = 0; i < 5; i++) {
      Serial.print(ModbusRTUServer.holdingRegisterRead(i));
      if (i < 4) Serial.print(", ");
    }
    Serial.println();
    lastPrint = millis();
  }
  ModbusRTUServer.poll();
}
