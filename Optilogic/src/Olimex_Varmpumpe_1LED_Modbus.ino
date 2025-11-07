#include <ModbusMaster.h>
#include <HardwareSerial.h>

#define RS485_TX_PIN 17
#define RS485_RX_PIN 16
#define RS485_DE_PIN 4
#define RS485_RE_PIN 4
#define GreenPin 3

ModbusMaster node;

String Modtaget_data = "off";

void preTransmission() {
  digitalWrite(RS485_DE_PIN, HIGH);
  //digitalWrite(RS485_RE_PIN, HIGH);
}

void postTransmission() {
  digitalWrite(RS485_DE_PIN, LOW);
  //digitalWrite(RS485_RE_PIN, LOW);
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);

  pinMode(RS485_DE_PIN, OUTPUT);
  pinMode(RS485_RE_PIN, OUTPUT);
  digitalWrite(RS485_DE_PIN, LOW);
  digitalWrite(RS485_RE_PIN, LOW);

  pinMode(GreenPin, OUTPUT);

  node.begin(1, Serial2);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  Serial.println("Modbus master setup complete");
}

void loop() {
  uint8_t result;

  while (Serial.available() > 0) {
    delay(10);  // Give some time for the entire message to arrive
    Modtaget_data = Serial.readStringUntil('\n');
    Modtaget_data.trim();  // Remove any whitespace
    //Serial.flush();        // Clear the serial buffer

    Serial.println("Received data: " + Modtaget_data);
    if (Modtaget_data == "on") {
      Serial.println("Anlæg tænder");
      result = node.writeSingleRegister(3, 5);

      if (result == node.ku8MBSuccess) {
        Serial.println("Successfully wrote 3 to register 368");
        digitalWrite(GreenPin, HIGH);
      } else {
        Serial.print("Modbus error: ");
        Serial.println(result);
      }
    } else if (Modtaget_data == "off") {
      Serial.println("Anlæg slukker");
      result = node.writeSingleRegister(3, 0);
      if (result == node.ku8MBSuccess) {
        Serial.println("Successfully wrote 0 to register 368");
        digitalWrite(GreenPin, LOW);
      } else {
        Serial.print("Modbus error: ");
        Serial.println(result);
      }
    }
  }
  /*  delay(1000);
  result = node.readHoldingRegisters(368, 1);
  if (result == node.ku8MBSuccess) {
    uint16_t registerValue = node.getResponseBuffer(0);
    Serial.print("Holding Register 368 value: ");
    Serial.println(registerValue);
  } else {
    Serial.print("Modbus read error: ");
    Serial.println(result);
  }
*/
  delay(1000);
}
