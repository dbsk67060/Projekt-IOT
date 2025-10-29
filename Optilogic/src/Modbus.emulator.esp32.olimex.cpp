// ==== ESP32 Modbus RTU MASTER over RS-485 (Olimex + MOD-RS485) ====
#include <ModbusRTU.h>

HardwareSerial RS485(2);

static const int RS485_TX   = 17;   // Justér efter din Olimex
static const int RS485_RX   = 16;
static const int RS485_DE_RE = 4;

ModbusRTU mb;

// Buffer til læste værdier
uint16_t regs[2] = {0};

bool cbRead(Modbus::ResultCode event, uint16_t transactionId, void* data) {
  if (event == Modbus::EX_SUCCESS) {
    Serial.printf("Read OK: Hreg0=%u, Hreg1=%u\n", regs[0], regs[1]);
  } else {
    Serial.printf("Read error: %02X\n", event);
  }
  return true;
}

void setup() {
  Serial.begin(115200);

  RS485.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
  mb.begin(&RS485, RS485_DE_RE);
  mb.setBaudrate(9600);

  mb.master();  // sæt som master
}

unsigned long lastPoll = 0;

void loop() {
  mb.task();

  unsigned long now = millis();
  if (now - lastPoll >= 1000 && !mb.slave()) {
    lastPoll = now;
    // Læs 2 holding registers fra slave id 1, startadresse 0
    // Resultatet lander i 'regs' og håndteres i cbRead
    mb.readHreg(1, 0, regs, 2, cbRead);
  }
}