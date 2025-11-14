#include <ModbusMaster.h>

ModbusMaster node;

// RS485 retning (DE/RE pin)
#define RS485_DIR_PIN 4

void preTransmission() {
  digitalWrite(RS485_DIR_PIN, HIGH);
}

void postTransmission() {
  digitalWrite(RS485_DIR_PIN, LOW);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starter Modbus RTU ventilatorstyring...");

  pinMode(RS485_DIR_PIN, OUTPUT);
  digitalWrite(RS485_DIR_PIN, LOW);

  // UART2: TX=17, RX=16
  Serial2.begin(9600, SERIAL_8N1, 16, 17);

  // Slave ID – typisk 1, tjek ventilatorens indstillinger
  node.begin(1, Serial2);

  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  delay(2000);

  Serial.println("Tænder ventilator (sætter til normal drift)...");
  uint8_t result = node.writeSingleRegister(3, 5); // Adresse 3 = "Mode", 5 = "Normal run"
  if (result == node.ku8MBSuccess) {
    Serial.println("Ventilator aktiveret!");
  } else {
    Serial.print("Fejl ved skrivning: 0x");
    Serial.println(result, HEX);
  }
}

void loop() {
  uint8_t result;
  uint16_t status;

  // Læs status fra adresse 3
  result = node.readHoldingRegisters(3, 1);
  if (result == node.ku8MBSuccess) {
    status = node.getResponseBuffer(0);
    Serial.print("Ventilatorstatus: ");
    Serial.print(status);
    Serial.print(" = ");

    switch (status) {
      case 0: Serial.println("Stopped"); break;
      case 1: Serial.println("Starting up"); break;
      case 2: Serial.println("Starting reduced speed"); break;
      case 3: Serial.println("Starting full speed"); break;
      case 4: Serial.println("Starting normal run"); break;
      case 5: Serial.println("Normal run"); break;
      case 6: Serial.println("Support control heating"); break;
      case 7: Serial.println("Support control cooling"); break;
      case 8: Serial.println("CO2 run"); break;
      case 9: Serial.println("Night cooling"); break;
      case 10: Serial.println("Full speed stop"); break;
      case 11: Serial.println("Stopping fan"); break;
      default: Serial.println("Ukendt status"); break;
    }
  } else {
    Serial.print("Fejl ved læsning: 0x");
    Serial.println(result, HEX);
  }

  delay(2000);
}
