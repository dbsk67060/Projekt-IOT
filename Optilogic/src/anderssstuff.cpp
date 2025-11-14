#include <ModbusMaster.h>

// ================ MODBUS COMMUNICATION CONFIGURATION ================
#define RX_PIN 36           // UART2 RX pin
#define TX_PIN 4            // UART2 TX pin
#define MAX485_DE 5         // RS485 Driver Enable pin
#define MAX485_RE_NEG 14    // RS485 Receiver Enable pin (active low)
#define BAUD_RATE 9600      // Communication speed
#define MODBUS_SLAVE_ID 1   // Slave device address

// ================ DATA BUFFERS ================
uint16_t holdingRegs[2];    // Buffer for holding registers (writable)
uint16_t inputRegs[2];      // Buffer for input registers (read-only)
char discreteStatus[2][10]; // Human-readable discrete input states

// Create Modbus master object
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
  // Initialize RS485 control pins
  pinMode(MAX485_RE_NEG, OUTPUT);
  pinMode(MAX485_DE, OUTPUT);
  digitalWrite(MAX485_RE_NEG, LOW);
  digitalWrite(MAX485_DE, LOW);

  // Start serial communication for debugging
  Serial.begin(115200);
  Serial.println("ESP32 Modbus RTU Communication Initializing...");

  // Configure UART2 for Modbus communication
  Serial2.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);

  // Initialize Modbus master
  modbus.begin(MODBUS_SLAVE_ID, Serial2);
  modbus.preTransmission(preTransmission);
  modbus.postTransmission(postTransmission);

  Serial.println("Modbus RTU Communication Initialized Successfully");
}

// =============== WRITE HOLDING REGISTERS ===============
// Write command to start the ventilation system
void writeSingleRegister_start() {
  holdingRegs[0] = 0; // Value for "start" command
  modbus.setTransmitBuffer(0, holdingRegs[0]);

  unsigned long startTime = millis();
  uint8_t result = modbus.writeSingleRegister(367, 0);
  unsigned long duration = millis() - startTime;

  if (result == modbus.ku8MBSuccess) {
    Serial.printf("Holding Reg[367] written = %u (Response time: %lums)\n",
                  holdingRegs[0], duration);
  } else {
    Serial.printf("Write Error (code %u) [Response time: %lums]\n", result, duration);
  }
}

// =============== READ INPUT REGISTERS ===============
// Read one input register (function 04)
void readtempregister() {
  unsigned long startTime = millis();
  uint8_t result = modbus.readInputRegisters(19, 1); 
  unsigned long duration = millis() - startTime;

  if (result == modbus.ku8MBSuccess) {
    inputRegs[0] = modbus.getResponseBuffer(0);

    // Dividing the temperature with 10 in order to get the correct value out
    float temperature = inputRegs[0] / 10.0f;
    Serial.printf("Input Reg[19] = %u (Temperature: %.1f C) (Response time: %lums)\n",
                  inputRegs[0], temperature, duration);
  } else {
    Serial.printf("Read Input Error (code %u) [Response time: %lums]\n", result, duration);
  }
}

void readPressure() {
  unsigned long startTime = millis();
  uint8_t result = modbus.readInputRegisters(291, 1); 
  unsigned long duration = millis() - startTime;

  if (result == modbus.ku8MBSuccess) {
    inputRegs[0] = modbus.getResponseBuffer(0);

    // Dividing the temperature with 10 in order to get the correct value out
    float pressure = inputRegs[0];
    Serial.printf("Input Reg[19] = %u (pressure:  psi) (Response time: %lums)\n",
                  inputRegs[0], pressure, duration);
  } else {
    Serial.printf("Read Input Error (code %u) [Response time: %lums]\n", result, duration);
  }
}

void readHumidity() {
  unsigned long startTime = millis();
  uint8_t result = modbus.readInputRegisters(22, 1); 
  unsigned long duration = millis() - startTime;

  if (result == modbus.ku8MBSuccess) {
    inputRegs[0] = modbus.getResponseBuffer(0);

    // Dividing the temperature with 10 in order to get the correct value out
    float humidity = inputRegs[0];
    Serial.printf("Input Reg[13] = %u (Humidity:  % ) (Response time: %lums)\n",
                  inputRegs[0], humidity, duration);
  } else {
    Serial.printf("Read Input Error (code %u) [Response time: %lums]\n", result, duration);
  }
}
// =============== LOOP ===============
void loop() {
  // Step 1: Write start command (holding reg 367)
  writeSingleRegister_start();

  // Step 2: Allow slave to process
  delay(300);

  // Step 3: Read input register 19
 readtempregister();
 delay(50);
  readHumidity();
delay(50);
  readPressure();
  
  // Diagnostic separator
  Serial.println("----------------------------");

  // Step 5: Wait before next Modbus cycle
  delay(2000);
}