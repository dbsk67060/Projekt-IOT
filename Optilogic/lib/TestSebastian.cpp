#include <ModbusMaster.h>

// ================ MODBUS COMMUNICATION CONFIGURATION ================
#define RX_PIN 36           // UART2 RX pin
#define TX_PIN 4            // UART2 TX pin
#define MAX485_DE 5         // RS485 Driver Enable pin
#define MAX485_RE_NEG 14    // RS485 Receiver Enable pin (active low)
#define BAUD_RATE 9600      // Communication speed
#define MODBUS_SLAVE_ID 1   // Slave device address

// Register-adresser (0-based i ModbusMaster)
#define PRESSURE_REG_ADDR 13   // EAF Pressure (Input Reg 14 i dokumentation)
#define TEMP_REG_ADDR     19   // Temperatur

uint16_t inputRegs[1];
ModbusMaster modbus;

// ================ RS485 Direction Control ================
void preTransmission() {
  digitalWrite(MAX485_RE_NEG, HIGH);  // Disable receiver
  digitalWrite(MAX485_DE, HIGH);      // Enable driver
}

void postTransmission() {
  digitalWrite(MAX485_RE_NEG, LOW);   // Enable receiver
  digitalWrite(MAX485_DE, LOW);       // Disable driver
}

void setup() {
  pinMode(MAX485_RE_NEG, OUTPUT);
  pinMode(MAX485_DE, OUTPUT);
  digitalWrite(MAX485_RE_NEG, LOW);
  digitalWrite(MAX485_DE, LOW);

  Serial.begin(115200);
  Serial.println("ESP32 Modbus RTU - Pressure (Pa) + Temperature");

  Serial2.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);

  modbus.begin(MODBUS_SLAVE_ID, Serial2);
  modbus.preTransmission(preTransmission);
  modbus.postTransmission(postTransmission);

  Serial.println("Modbus RTU Initialized\n");
}

// =============== READ TEMPERATURE ===============
void readTemperature() {
  uint8_t result = modbus.readInputRegisters(TEMP_REG_ADDR, 1);
  if (result == modbus.ku8MBSuccess) {
    uint16_t raw = modbus.getResponseBuffer(0);
    float temperature = raw / 10.0f;   // som din gamle kode

    Serial.printf("Temperature: %.1f °C (raw: %u)\n", temperature, raw);
  } else {
    Serial.printf("Temperature Read Error (code %u)\n", result);
  }
}

// =============== READ PRESSURE IN PASCAL ===============
void readPressure() {
  uint8_t result = modbus.readInputRegisters(PRESSURE_REG_ADDR, 1);
  if (result == modbus.ku8MBSuccess) {
    uint16_t raw = modbus.getResponseBuffer(0);

    float pressurePa   = raw;              // 1 count = 1 Pa
    float pressuremBar = pressurePa / 100.0f;

    Serial.printf("Pressure: %.1f Pa (%.2f mbar) (raw: %u)\n",
                  pressurePa, pressuremBar, raw);
  } else {
    Serial.printf("Pressure Read Error (code %u)\n", result);
  }
}

// =============== LOOP ===============
void loop() {
  readTemperature();
  readPressure();

  Serial.println("----------------------------");
  delay(1000);
}
