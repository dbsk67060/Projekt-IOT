#include <ModbusMaster.h>

// =====================================================================
//  HARDWARE & MODBUS KONFIGURATION
// =====================================================================

// RS485 / UART pins på ESP32 (tilpas evt. til jeres board)
#define RX_PIN        36        // UART2 RX (fra RS485 transceiver RO)
#define TX_PIN        4         // UART2 TX (til RS485 transceiver DI)
#define MAX485_DE     5         // RS485 Driver Enable
#define MAX485_RE_NEG 14        // RS485 Receiver Enable (active low)

// Modbus-indstillinger
#define BAUD_RATE       9600
#define MODBUS_SLAVE_ID 1       // Slave-adresse på ventilationsanlægget

// =====================================================================
//  REGISTRE (0-BASED ADRESSER TIL KODEN)
//  NOTE: I jeres dokument er adresserne 1-baserede.
//        I koden bruger vi 0-baseret:  codeReg = docReg - 1
// =====================================================================

// ---------------- Analoge kanaler (typer) ----------------
// VentSettings.Cor_Ai1..Ai4, UAi1 = Input Reg 34–38 (dokument)
// Kode: 34–38  →  33–37
#define REG_AI1_TYPE   33   // doc 34: AI1 type (0–19)
#define REG_AI2_TYPE   34   // doc 35: AI2 type (0–19)
// Hvis I senere vil bruge AI3/AI4/UAi1:
// #define REG_AI3_TYPE   35   // doc 36
// #define REG_AI4_TYPE   36   // doc 37
// #define REG_UAI1_TYPE  37   // doc 38

// ---------------- Analoge kanaler (værdier) ----------------
// VentActual.Cor_AnalogInput1..5 = Input Reg 26–30 (dokument)
// Kode: 26–30 → 25–29
#define REG_AI1_VALUE  25   // doc 26: AnalogInput1  (AI1 værdi)
#define REG_AI2_VALUE  26   // doc 27: AnalogInput2  (AI2 værdi)
// Hvis I senere vil bruge flere:
// #define REG_AI3_VALUE  27   // doc 28: AnalogInput3
// #define REG_AI4_VALUE  28   // doc 29: AnalogInput4
// #define REG_UAI1_VALUE 29   // doc 30: AnalogInput5 (UAI1)

// ---------------- Direkte værdier (valgfrit) ----------------
// Disse brugte I tidligere – I kan slå dem til/fra med defines
#define USE_DIRECT_TEMP       1
#define USE_DIRECT_EAF_PRESS  1

// Temperatur (virker hos jer)
// I har brugt reg 19 før → vi holder fast i den:
#define REG_SUPPLY_TEMP   19   // doc 20?  temp i /10 °C

// EAF-tryk (virkede når anlægget var tændt)
// VentActual.Cor_EAFPressure = Input Reg 14 (dokument) → 13 i kode
#define REG_EAF_PRESSURE  13   // doc 14: EAF pressure i Pa

// =====================================================================
//  GLOBALER
// =====================================================================

ModbusMaster modbus;

// Vi laver en lille struktur for at holde styr på AI1/AI2
struct AiChannel {
  const char* name;      // "AI1", "AI2" ...
  uint16_t typeReg;      // register-adresse for type (0–19)
  uint16_t valueReg;     // register-adresse for værdi
  uint16_t typeCode;     // senest læste type (0–19)
  bool     available;    // om type-registret kan læses
};

// Kun AI1 & AI2 lige nu (nemt at udvide)
AiChannel channels[] = {
  { "AI1", REG_AI1_TYPE, REG_AI1_VALUE, 0, false },
  { "AI2", REG_AI2_TYPE, REG_AI2_VALUE, 0, false }
};

const int NUM_CHANNELS = sizeof(channels)/sizeof(channels[0]);

// =====================================================================
//  RS485 RETNINGSSTYRING
// =====================================================================

void preTransmission() {
  digitalWrite(MAX485_RE_NEG, HIGH);  // Disable receiver
  digitalWrite(MAX485_DE, HIGH);      // Enable driver (TX)
}

void postTransmission() {
  digitalWrite(MAX485_RE_NEG, LOW);   // Enable receiver (RX)
  digitalWrite(MAX485_DE, LOW);       // Disable driver
}

// =====================================================================
//  HJÆLPEFUNKTIONER
// =====================================================================

// Oversætter type kode (0–19) til tekst
const char* signalName(uint16_t code) {
  switch (code) {
    case 0:  return "Not used";
    case 1:  return "Outdoortemp";
    case 2:  return "Supplytemp";
    case 3:  return "Extracttemp";
    case 4:  return "Roomtemp1";
    case 5:  return "Roomtemp2";
    case 6:  return "Exhausttemp";
    case 7:  return "Extrasensor";
    case 8:  return "SAF pressure";
    case 9:  return "EAF pressure";
    case 10: return "Deicingtemp";
    case 11: return "Frost prot.temp";
    case 12: return "CO2";
    case 13: return "Humidity room";
    case 14: return "Humidity duct";
    case 15: return "Extra unit temp";
    case 16: return "External SAF control";
    case 17: return "External EAF control";
    case 18: return "SAF pressure 2";
    case 19: return "Humidity outdoor";
    default: return "Unknown";
  }
}

// Læs ét input register (helper) – bruger vi overalt
bool readInputReg(uint16_t reg, uint16_t &out) {
  uint8_t result = modbus.readInputRegisters(reg, 1);
  if (result == modbus.ku8MBSuccess) {
    out = modbus.getResponseBuffer(0);
    return true;
  } else {
    // 226 ≈ timeout → ingen svar fra slaven
    Serial.printf("Read error on reg %u (code %u)\n", reg, result);
    return false;
  }
}

// =====================================================================
//  AI: LÆS TYPE & VÆRDI
// =====================================================================

// Læs type for én kanal (f.eks. AI1)
void updateChannelType(AiChannel &ch) {
  uint16_t raw = 0;
  if (readInputReg(ch.typeReg, raw)) {
    ch.typeCode = raw;
    ch.available = true;
    Serial.printf("%s type: code %u → %s (reg %u)\n",
                  ch.name, raw, signalName(raw), ch.typeReg);
  } else {
    ch.typeCode = 0;
    ch.available = false;
    Serial.printf("%s type: NOT AVAILABLE (reg %u)\n",
                  ch.name, ch.typeReg);
  }
}

// Læs og fortolk værdi for én kanal
void readChannelValue(const AiChannel &ch) {
  if (!ch.available) {
    // Kanal findes ikke / svarer ikke på type-registret → spring over
    return;
  }

  uint16_t raw = 0;
  if (!readInputReg(ch.valueReg, raw)) {
    return;
  }

  float value = raw;
  const char* unit = "units";

  switch (ch.typeCode) {
    // Temperatur-signaler – typisk /10 °C
    case 1:  // Outdoortemp
    case 2:  // Supplytemp
    case 3:  // Extracttemp
    case 4:  // Roomtemp1
    case 5:  // Roomtemp2
    case 6:  // Exhausttemp
    case 10: // Deicingtemp
    case 11: // Frost prot.temp
    case 15: // Extra unit temp
      value = raw / 10.0f;
      unit  = "°C";
      break;

    // Tryk-signaler – hos jer: 1 count = 1 Pa
    case 8:  // SAF pressure
    case 9:  // EAF pressure
    case 18: // SAF pressure 2
      value = raw;
      unit  = "Pa";
      break;

    // Fugt – gættet som direkte %RH (ret til /10 hvis nødvendigt)
    case 13: // Humidity room
    case 14: // Humidity duct
    case 19: // Humidity outdoor
      value = raw;
      unit  = "%RH";
      break;

    // CO2 – typisk ppm
    case 12:
      value = raw;
      unit  = "ppm";
      break;

    // Alt andet / ukendt – bare rå værdi
    default:
      value = raw;
      unit  = "units";
      break;
  }

  Serial.printf("%s (%s): %.1f %s (raw: %u, typeCode: %u, typeReg: %u, valReg: %u)\n",
                ch.name,
                signalName(ch.typeCode),
                value, unit,
                raw,
                ch.typeCode,
                ch.typeReg,
                ch.valueReg);
}

// =====================================================================
//  VALGFRIT: DIREKTE TEMP & EAF-PRESSURE (IKKE VIA AI1/AI2)
// =====================================================================
void startVentilation() {
  // 3 = On (hvis det er det jeres register kræver)
  uint16_t commandValue = 3;

  // Indsæt værdi i Modbus buffer
  modbus.setTransmitBuffer(0, commandValue);

  unsigned long startTime = millis();
  uint8_t result = modbus.writeSingleRegister(367, commandValue);
  unsigned long duration = millis() - startTime;

  if (result == modbus.ku8MBSuccess) {
    Serial.printf("Ventilation START sent. Holding Reg[367] = %u (Response time: %lums)\n",
                  commandValue, duration);
  } else {
    Serial.printf("Start command ERROR (code %u) [Response time: %lums]\n",
                  result, duration);
  }
}

#if USE_DIRECT_TEMP
void readDirectTemperature() {
  uint16_t raw = 0;
  if (!readInputReg(REG_SUPPLY_TEMP, raw)) return;

  float temperature = raw / 10.0f;
  Serial.printf("Direct temp (reg %u): %.1f °C (raw %u)\n",
                REG_SUPPLY_TEMP, temperature, raw);
}
#endif

#if USE_DIRECT_EAF_PRESS
void readDirectPressure() {
  uint16_t raw = 0;
  uint8_t result = modbus.readInputRegisters(REG_EAF_PRESSURE, 1);

  if (result == modbus.ku8MBSuccess) {
    raw = modbus.getResponseBuffer(0);
    float pa   = raw;
    float mbar = pa / 100.0f;

    Serial.printf("Direct EAF pressure (reg %u): %.1f Pa (%.2f mbar, raw %u)\n",
                  REG_EAF_PRESSURE, pa, mbar, raw);
  } else if (result == 226) {
    // Typisk timeout → anlæg evt. slukket
    Serial.println("Direct EAF pressure: ingen svar (timeout, anlæg/slave slukket?)");
  } else {
    Serial.printf("Direct EAF pressure read error (code %u)\n", result);
  }
}
#endif

// =====================================================================
//  SETUP & LOOP
// =====================================================================

unsigned long lastTypeUpdate = 0;
const unsigned long TYPE_UPDATE_INTERVAL_MS = 30000; // opdater typer hver 30 sek

void setup() {
  pinMode(MAX485_RE_NEG, OUTPUT);
  pinMode(MAX485_DE, OUTPUT);
  digitalWrite(MAX485_RE_NEG, LOW);
  digitalWrite(MAX485_DE, LOW);

  Serial.begin(115200);
  Serial.println("\n=== ESP32 Modbus – Ryddet AI1/AI2 Reader ===");

  Serial2.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);

  modbus.begin(MODBUS_SLAVE_ID, Serial2);
  modbus.preTransmission(preTransmission);
  modbus.postTransmission(postTransmission);

  Serial.println("Modbus RTU initialized");

  startVentilation();

  // Læs type for AI1 & AI2 ved opstart
  for (int i = 0; i < NUM_CHANNELS; i++) {
    updateChannelType(channels[i]);
  }
  lastTypeUpdate = millis();

  Serial.println("------------------------------------------");
}

void loop() {
  // Læs og vis AI1 & AI2
  for (int i = 0; i < NUM_CHANNELS; i++) {
    readChannelValue(channels[i]);
  }

  // Valgfrit: direkte temp / EAF-pressure
  #if USE_DIRECT_TEMP
    readDirectTemperature();
  #endif

  #if USE_DIRECT_EAF_PRESS
    readDirectPressure();
  #endif

  Serial.println("==========================================");
  delay(1000);

  // Opdater typer med mellemrum (hvis de kan ændres i menuen)
  unsigned long now = millis();
  if (now - lastTypeUpdate > TYPE_UPDATE_INTERVAL_MS) {
    Serial.println("Updating channel types...");
    for (int i = 0; i < NUM_CHANNELS; i++) {
      updateChannelType(channels[i]);
    }
    lastTypeUpdate = now;
    Serial.println("------------------------------------------");
  }
}
