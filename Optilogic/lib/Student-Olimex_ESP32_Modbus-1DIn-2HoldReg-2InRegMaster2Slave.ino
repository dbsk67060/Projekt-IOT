#include <ModbusMaster.h>

// ================ MODBUS COMMUNICATION CONFIGURATION ================
#define RX_PIN 36           // UART2 RX pin
#define TX_PIN 4            // UART2 TX pin
#define MAX485_DE 5         // RS485 Driver Enable pin
#define MAX485_RE_NEG 14    // RS485 Receiver Enable pin (active low)
#define BAUD_RATE 9600      // Communication speed
#define MODBUS_SLAVE_ID 4  // Slave device address

// ================ DATA BUFFERS ================
uint16_t holdingRegs[2];    // Buffer for holding registers (writable)
uint16_t inputRegs[2];      // Buffer for input registers (read-only)
bool discreteInputs[2];     // Buffer for discrete inputs (binary status)
char frontDoorStatus[10];   // Human-readable front door status
char backDoorStatus[10];    // Human-readable back door status

// Create Modbus master object
ModbusMaster modbus;

// Function to prepare for data transmission
void preTransmission() {
  digitalWrite(MAX485_RE_NEG, HIGH);  // Disable receiver
  digitalWrite(MAX485_DE, HIGH);      // Enable driver
}

// Function to clean up after data transmission
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

  // Initialize Modbus master with slave ID and UART2
  modbus.begin(MODBUS_SLAVE_ID, Serial2);

  // Set pre- and post-transmission callbacks for RS485 control
  modbus.preTransmission(preTransmission);
  modbus.postTransmission(postTransmission);

  Serial.println("Modbus RTU Communication Initialized Successfully");
}

// Function to read input registers (e.g., motor RPM)
void readInputRegisters() {
  // Attempt to read 1 input register starting at address 0
  if (modbus.readInputRegisters(0, 1) == modbus.ku8MBSuccess) {
    inputRegs[0] = modbus.getResponseBuffer(0);
    Serial.printf("Motor RPM: %d\n", inputRegs[0]);
  } else {
    Serial.println("ERROR: Failed to read input registers");
  }
}

// Function to read discrete inputs (e.g., door statuses)
void readDiscreteInputs() {
  // Attempt to read 2 discrete inputs starting at address 1
  if (modbus.readDiscreteInputs(1, 2) == modbus.ku8MBSuccess) {
    // Store door states (note: response buffer indexing starts at 0)
    discreteInputs[0] = modbus.getResponseBuffer(0);
    discreteInputs[1] = modbus.getResponseBuffer(1);

    // Convert boolean states to human-readable strings
    snprintf(frontDoorStatus, sizeof(frontDoorStatus), "%s", discreteInputs[0] ? "Open" : "Closed");
    snprintf(backDoorStatus, sizeof(backDoorStatus), "%s", discreteInputs[1] ? "Closed" : "Open");

    // Display door statuses
    Serial.printf("Front Door: %s\n", frontDoorStatus);
    Serial.printf("Back Door: %s\n", backDoorStatus);
  } else {
    Serial.println("ERROR: Failed to read discrete inputs");
  }
}

// Function to write holding registers (e.g., setpoints)
void writeHoldingRegisters() {
  // Prepare setpoint values
  holdingRegs[0] = 30;  // Temperature setpoint (°C)
  holdingRegs[1] = 25;  // Humidity setpoint (%)

  // Load transmit buffer with setpoint values
  modbus.setTransmitBuffer(0, holdingRegs[0]);
  modbus.setTransmitBuffer(1, holdingRegs[1]);

  // Attempt to write 2 holding registers starting at address 3
  if (modbus.writeMultipleRegisters(3, 2) == modbus.ku8MBSuccess) {
    Serial.println("Holding Registers Updated Successfully");
  } else {
    Serial.println("ERROR: Failed to write holding registers");
  }
}

void loop() {
  // Perform Modbus read and write operations
  readInputRegisters();
  readDiscreteInputs();
  writeHoldingRegisters();

  // Add diagnostic separator for readability
  Serial.println("----------------------------");

  // Delay between communication cycles to prevent overwhelming the bus
  delay(2000);  // 2-second interval
}
