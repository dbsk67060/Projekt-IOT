#include <ModbusMaster.h>

// ================= MODBUS / RS485 CONFIG =================
#define RX_PIN 36
#define TX_PIN 4
#define MAX485_DE 5
#define MAX485_RE_NEG 14
#define BAUD_RATE 9600
#define MODBUS_SLAVE_ID 1

ModbusMaster modbus;

// ================= RS485 CONTROL =================
void preTransmission() {
  digitalWrite(MAX485_RE_NEG, HIGH);
  digitalWrite(MAX485_DE, HIGH);
}

void postTransmission() {
  digitalWrite(MAX485_RE_NEG, LOW);
  digitalWrite(MAX485_DE, LOW);
}

// ================= GENERIC FUNCTIONS =================
uint16_t readInputReg(uint16_t address) {
  uint8_t r = modbus.readInputRegisters(address, 1);
  if (r == modbus.ku8MBSuccess) return modbus.getResponseBuffer(0);
  return 0;
}

void writeHoldingReg(uint16_t address, uint16_t value) {
  modbus.setTransmitBuffer(0, value);
  modbus.writeSingleRegister(address, value);
}

// ================= SPECIALIZED FUNCTIONS =================
void fanStart() {
  writeHoldingReg(367, 0);
}

float readTemperature() {
  uint16_t raw = readInputReg(19);
  return raw / 10.0f;
}

float readSAFPressure2() {
    uint16_t regs[11]; // reg 10–20
    uint8_t result = modbus.readInputRegisters(10, 11);
    if (result != modbus.ku8MBSuccess) return 0;
    for (int i=0; i<11; i++) regs[i] = modbus.getResponseBuffer(i);
    return regs[3] / 10.0f;  // index 3 = register 13
}



float readSAFAirFlow2() {
  uint16_t raw = readInputReg(15);
  return raw;
}

// ================= SETUP =================
void setup() {
  pinMode(MAX485_RE_NEG, OUTPUT);
  pinMode(MAX485_DE, OUTPUT);
  digitalWrite(MAX485_RE_NEG, LOW);
  digitalWrite(MAX485_DE, LOW);

  Serial.begin(115200);
  Serial2.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);

  modbus.begin(MODBUS_SLAVE_ID, Serial2);
  modbus.preTransmission(preTransmission);
  modbus.postTransmission(postTransmission);
}

// ================= LOOP =================
void loop() {
  fanStart();
  delay(300);

  float t   = readTemperature();
  float p   = readSAFPressure2();
  float af  = readSAFAirFlow2();

  // Debug scan af input-registers
  for (int i = 10; i <= 20; i++) {
    uint16_t v = readInputReg(i);
    Serial.printf("Reg %d = %u\n", i, v);
  }
  Serial.println("-----");

  Serial.printf("Temperature: %.1f C\n", t);
  Serial.printf("SAF Pressure2: %.1f\n", p);
  Serial.printf("SAF AirFlow2: %.1f\n", af);

  Serial.println("----------------------");
  delay(2000);
}
// ================= END OF FILE =================
