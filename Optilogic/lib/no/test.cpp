#include <ModbusRTU.h>


// UART2 loopback test (uden RS-485): TX17 -> RX16 forbundet med jumper
HardwareSerial RS485(2);
void setup() {
  Serial.begin(115200);
  RS485.begin(9600, SERIAL_8N1, 16, 17);
  Serial.println("UART2 loopback: taster du i Serial Monitor, skal samme tekst echos tilbage hver 1s.");
}
void loop() {
  while (Serial.available()) RS485.write(Serial.read());
  while (RS485.available()) Serial.write(RS485.read());
  delay(10);
}