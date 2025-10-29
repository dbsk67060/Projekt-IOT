// ==== ESP32 Modbus RTU MASTER over RS-485 (Olimex + MOD-RS485) ====
// Clean, single-definition version (verbose logging)
// Library: ModbusRTU by Alexander Emelianov

#include <ModbusRTU.h>

// UART2 on ESP32 used via UEXT1 on Olimex boards
HardwareSerial RS485(2);

// Adjust pins if your specific Olimex model maps UEXT UART differently
static const int RS485_TX    = 17;   // ESP32 TX -> RS485 DI
static const int RS485_RX    = 16;   // ESP32 RX <- RS485 RO
static const int RS485_DE_RE = 33;   // ESP32-POE UEXT pin7 -> RS485 DE/RE (HIGH=TX, LOW=RX)

ModbusRTU mb;

// Buffer for Holding Registers we read from the Windows slave
uint16_t regs[2] = {0};

// Pretty-print Modbus errors
static const char* mbErr2str(Modbus::ResultCode e) {
  switch (e) {
    case Modbus::EX_SUCCESS:             return "EX_SUCCESS";
    case Modbus::EX_ILLEGAL_FUNCTION:    return "EX_ILLEGAL_FUNCTION";
    case Modbus::EX_ILLEGAL_ADDRESS:     return "EX_ILLEGAL_ADDRESS";
    case Modbus::EX_ILLEGAL_VALUE:       return "EX_ILLEGAL_VALUE";
    case Modbus::EX_SLAVE_FAILURE:       return "EX_SLAVE_FAILURE";
    case Modbus::EX_ACKNOWLEDGE:         return "EX_ACKNOWLEDGE";
    case Modbus::EX_SLAVE_DEVICE_BUSY:   return "EX_SLAVE_DEVICE_BUSY";
    case Modbus::EX_MEMORY_PARITY_ERROR: return "EX_MEMORY_PARITY_ERROR";
    case Modbus::EX_TIMEOUT:             return "EX_TIMEOUT";
    default:                             return "EX_UNKNOWN"; // some library versions omit certain TCP-only codes
  }
}

// Callback invoked when the read completes
static bool cbRead(Modbus::ResultCode event, uint16_t transactionId, void* data) {
  const unsigned long t = millis();
  if (event == Modbus::EX_SUCCESS) {
    Serial.printf("[%lu ms] Read OK (tx=%u) -> Hreg0=%u (0x%04X), Hreg1=%u (0x%04X)\n",
                  t, transactionId, regs[0], regs[0], regs[1], regs[1]);
  } else {
    Serial.printf("[%lu ms] Read ERROR (tx=%u): %s (0x%02X)\n",
                  t, transactionId, mbErr2str(event), event);
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println(F("=== Modbus RTU MASTER (ESP32 -> Windows slave) ==="));
  Serial.println(F("UART2 pins: TX=17, RX=16, RE/DE=33, 9600 8N1"));
  Serial.println(F("Polling slave ID 1: Holding Registers 0..1 hver 1s"));

  // Bring up RS-485 UART and Modbus master
  RS485.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
  RS485.setTimeout(2000);
  mb.begin(&RS485, RS485_DE_RE);   // library controls DE/RE automatically
  mb.setBaudrate(9600);
  mb.master();
}

static unsigned long lastPoll = 0;

void loop() {
  mb.task();

  const unsigned long now = millis();
  if (now - lastPoll >= 1000 && !mb.slave()) {
    lastPoll = now;

    Serial.printf("[%lu ms] -> Request: readHreg(slave=1, addr=0, qty=2)\n", now);

    // Read 2 Holding Registers starting at address 0 from slave id 1
    const bool queued = mb.readHreg(1, 0, regs, 2, cbRead);
    if (!queued) {
      Serial.println(F("Kunne ikke queue Modbus-forespørgslen (busy?)"));
    }
  }
}