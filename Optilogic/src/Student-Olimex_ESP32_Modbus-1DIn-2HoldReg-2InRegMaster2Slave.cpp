#ifndef SERIAL_CONFIG
#define SERIAL_CONFIG SERIAL_8N1  // default; change to SERIAL_8E1 if needed
#endif
#include <ModbusMaster.h>

// ================ MODBUS COMMUNICATION CONFIGURATION ================
#define RX_PIN 16           // UART2 RX pin
#define TX_PIN 17            // UART2 TX pin
#define MAX485_RE_DE 22      // RS485 Driver/Receiver control (HIGH=TX, LOW=RX)
#define BAUD_RATE 9600      // Communication speed
#define MODBUS_SLAVE_ID 1  // Slave device address

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
  // Ensure bus is idle before enabling driver
  delayMicroseconds(200);
  digitalWrite(MAX485_RE_DE, HIGH);  // Driver on, Receiver off
  // Allow driver to settle before sending first byte
  delayMicroseconds(200);
}

// Function to clean up after data transmission
void postTransmission() {
  // Make sure all bytes have left the UART shift register
  Serial2.flush();
  // Guard time after last byte to satisfy t3.5 (~3.5 char times)
  delayMicroseconds(400);
  digitalWrite(MAX485_RE_DE, LOW);   // Receiver on, Driver off
  // Allow the transceiver to switch to RX before the slave starts to answer
  delayMicroseconds(200);
}

// === Autoprobe candidates
struct UartCfg { uint32_t baud; uint32_t framing; const char* name; };
UartCfg UART_CANDIDATES[] = {
  {4800,  SERIAL_8N1,  "4800-8N1"},
  {4800,  SERIAL_8E1,  "4800-8E1"},
#ifdef SERIAL_8O1
  {4800,  SERIAL_8O1,  "4800-8O1"},
#endif
  {9600,  SERIAL_8N1,  "9600-8N1"},
  {9600,  SERIAL_8E1,  "9600-8E1"},
#ifdef SERIAL_8O1
  {9600,  SERIAL_8O1,  "9600-8O1"},
#endif
  {19200, SERIAL_8N1,  "19200-8N1"},
  {19200, SERIAL_8E1,  "19200-8E1"},
#ifdef SERIAL_8O1
  {19200, SERIAL_8O1,  "19200-8O1"},
#endif
  {38400, SERIAL_8N1,  "38400-8N1"},
  {38400, SERIAL_8E1,  "38400-8E1"},
#ifdef SERIAL_8O1
  {38400, SERIAL_8O1,  "38400-8O1"},
#endif
};
uint8_t ID_CANDIDATES[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};

// Active config (initialized from defines, may be overridden by autoProbe)
uint32_t activeBaud = BAUD_RATE;
uint32_t activeFraming = SERIAL_CONFIG;
uint8_t  activeId = MODBUS_SLAVE_ID;

bool tryReadOnce() {
  uint8_t res = modbus.readInputRegisters(0, 1);
  if (res == modbus.ku8MBSuccess) {
    inputRegs[0] = modbus.getResponseBuffer(0);
    return true;
  }
  // Non-intrusive peek at the UART RX buffer after the transaction
  int avail = Serial2.available();
  if (avail > 0) {
    Serial.printf("  (diag) saw %d raw bytes on RX but parse failed\n", avail);
    while (Serial2.available()) Serial2.read();
  }
  return false;
}

bool probeCombo(uint32_t baud, uint32_t framing, uint8_t id) {
  Serial2.end();
  delay(20);
  Serial2.begin(baud, framing, RX_PIN, TX_PIN);
  Serial2.setTimeout(200);
  while (Serial2.available()) Serial2.read();
  modbus.begin(id, Serial2);
  modbus.preTransmission(preTransmission);
  modbus.postTransmission(postTransmission);
  // small settle time
  delay(50);
  // a couple of attempts per combo
  for (int i=0;i<3;i++) {
    if (tryReadOnce()) return true;
    delay(60);
  }
  return false;
}

void autoProbe()
{
  Serial.println("Auto-probing Modbus (baud/parity/ID)...");
  for (unsigned i=0;i<sizeof(UART_CANDIDATES)/sizeof(UartCfg); ++i) {
    for (unsigned j=0;j<sizeof(ID_CANDIDATES)/sizeof(ID_CANDIDATES[0]); ++j) {
      uint8_t id = ID_CANDIDATES[j];
      Serial.printf("  Trying %s, ID=%u... ", UART_CANDIDATES[i].name, id);
      bool ok = probeCombo(UART_CANDIDATES[i].baud, UART_CANDIDATES[i].framing, id);
      if (ok) {
        Serial.println("OK");
        activeBaud = UART_CANDIDATES[i].baud;
        activeFraming = UART_CANDIDATES[i].framing;
        activeId = id;
        Serial.printf("LOCKED: %s ID=%u, first inputReg0=%u\n", UART_CANDIDATES[i].name, id, inputRegs[0]);
        return;
      } else {
        Serial.println("no response");
      }
    }
  }
  Serial.println("Auto-probing failed. Check wiring/A-B/GND/termination and register addresses.");
}

// ===== Serial framing config (many Modbus RTU slaves default to 8E1). Change if needed.
#ifndef SERIAL_CONFIG
#define SERIAL_CONFIG SERIAL_8N1  // Try switching to SERIAL_8E1 if your slave uses even parity
#endif

// Optional: tune Modbus timing
#ifndef MODBUS_TIMEOUT_MS
#define MODBUS_TIMEOUT_MS 2000
#endif

#ifndef MODBUS_RETRIES
#define MODBUS_RETRIES 1
#endif

const char* framingToName(uint32_t f) {
  if (f == SERIAL_8N1) return "8N1";
  if (f == SERIAL_8E1) return "8E1";
#ifdef SERIAL_8O1
  if (f == SERIAL_8O1) return "8O1";
#endif
  return "?";
}

void setup() {
  // Initialize RS485 control pin
  pinMode(MAX485_RE_DE, OUTPUT);
  digitalWrite(MAX485_RE_DE, LOW);   // idle in receive mode

  // Start serial communication for debugging
  Serial.begin(115200);
  Serial.println("ESP32 Modbus RTU Communication Initializing...");

  // Configure UART2 for Modbus communication
  Serial2.begin(BAUD_RATE, SERIAL_CONFIG, RX_PIN, TX_PIN);
  Serial2.setTimeout(200);
  while (Serial2.available()) Serial2.read();

  // Initialize Modbus master with slave ID and UART2
  modbus.begin(MODBUS_SLAVE_ID, Serial2);

  // Set pre- and post-transmission callbacks for RS485 control
  modbus.preTransmission(preTransmission);
  modbus.postTransmission(postTransmission);

  // Optional: try to discover working baud/parity/ID
  autoProbe();

  // Re-apply the active config for the rest of the program
  Serial2.end();
  delay(20);
  Serial2.begin(activeBaud, activeFraming, RX_PIN, TX_PIN);
  modbus.begin(activeId, Serial2);
  modbus.preTransmission(preTransmission);
  modbus.postTransmission(postTransmission);

  Serial.printf("Modbus RTU ready: baud=%lu, framing=%s, slaveID=%u\n", (unsigned long)activeBaud, framingToName(activeFraming), activeId);
}

// Helper: print detailed Modbus error cause
void printModbusError(uint8_t result, const char* when) {
  if (result == modbus.ku8MBSuccess) return;
  Serial.printf("%s -> Modbus error 0x%02X: ", when, result);
  switch (result) {
    case ModbusMaster::ku8MBIllegalFunction: Serial.println("Illegal function"); break;
    case ModbusMaster::ku8MBIllegalDataAddress: Serial.println("Illegal data address (wrong register)"); break;
    case ModbusMaster::ku8MBIllegalDataValue: Serial.println("Illegal data value"); break;
    case ModbusMaster::ku8MBSlaveDeviceFailure: Serial.println("Slave device failure"); break;
    case ModbusMaster::ku8MBInvalidCRC: Serial.println("Invalid CRC (noise/wiring)"); break;
    case ModbusMaster::ku8MBResponseTimedOut: Serial.println("Response timed out (ID/parity/baud/wiring)"); break;
    default: Serial.println("Unknown error"); break;
  }
}

// Function to read input registers (e.g., motor RPM)
void readInputRegisters() {
  while (Serial2.available()) Serial2.read();
  // Attempt to read 1 input register starting at address 0 (note: device docs often show 30001 -> address 0)
  uint8_t res = 0xFF;
  for (int attempt = 0; attempt < MODBUS_RETRIES; ++attempt) {
    res = modbus.readInputRegisters(0, 1);
    if (res == modbus.ku8MBSuccess) break;
    delay(50);
  }
  if (res == modbus.ku8MBSuccess) {
    inputRegs[0] = modbus.getResponseBuffer(0);
    Serial.printf("Motor RPM: %u\n", inputRegs[0]);
  } else {
    Serial.println("ERROR: Failed to read input registers");
    printModbusError(res, "readInputRegisters");
  }
}

// Function to read discrete inputs (e.g., door statuses)
void readDiscreteInputs() {
  while (Serial2.available()) Serial2.read();
  // Attempt to read 2 discrete inputs starting at address 1
  uint8_t res = 0xFF;
  for (int attempt = 0; attempt < MODBUS_RETRIES; ++attempt) {
    res = modbus.readDiscreteInputs(1, 2);
    if (res == modbus.ku8MBSuccess) break;
    delay(50);
  }
  if (res == modbus.ku8MBSuccess) {
    uint16_t word0 = modbus.getResponseBuffer(0);
    // Discrete inputs are bit-packed; bit 0 -> first input, bit 1 -> second input
    discreteInputs[0] = bitRead(word0, 0);
    discreteInputs[1] = bitRead(word0, 1);

    // Convert boolean states to human-readable strings
    snprintf(frontDoorStatus, sizeof(frontDoorStatus), "%s", discreteInputs[0] ? "Open" : "Closed");
    snprintf(backDoorStatus, sizeof(backDoorStatus), "%s", discreteInputs[1] ? "Open" : "Closed");

    // Display door statuses
    Serial.printf("Front Door: %s\n", frontDoorStatus);
    Serial.printf("Back Door: %s\n", backDoorStatus);
  } else {
    Serial.println("ERROR: Failed to read discrete inputs");
    printModbusError(res, "readDiscreteInputs");
  }
}

// Function to write holding registers (e.g., setpoints)
void writeHoldingRegisters() {
  while (Serial2.available()) Serial2.read();
  // Prepare setpoint values – adjust addresses and values to match your slave's map
  holdingRegs[0] = 30;  // Temperature setpoint (°C)
  holdingRegs[1] = 25;  // Humidity setpoint (%)

  // Load transmit buffer with setpoint values
  modbus.setTransmitBuffer(0, holdingRegs[0]);
  modbus.setTransmitBuffer(1, holdingRegs[1]);

  // Attempt to write 2 holding registers starting at address 3 (note: many datasheets show 40004 -> address 3)
  uint8_t res = 0xFF;
  for (int attempt = 0; attempt < MODBUS_RETRIES; ++attempt) {
    res = modbus.writeMultipleRegisters(3, 2);
    if (res == modbus.ku8MBSuccess) break;
    delay(50);
  }
  if (res == modbus.ku8MBSuccess) {
    Serial.println("Holding Registers Updated Successfully");
  } else {
    Serial.println("ERROR: Failed to write holding registers");
    printModbusError(res, "writeHoldingRegisters");
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
