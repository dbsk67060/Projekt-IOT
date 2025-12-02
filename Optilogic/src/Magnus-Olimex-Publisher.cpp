#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ModbusMaster.h>

// NOTE: Ensure PubSubClient library is installed (Arduino Library Manager or PlatformIO lib_deps).
// ================= MODBUS / RS485 CONFIG =================
#define RX_PIN 36
#define TX_PIN 4
#define MAX485_DE 5
#define MAX485_RE_NEG 14
#define BAUD_RATE 9600
#define MODBUS_SLAVE_ID 1

ModbusMaster modbus;

// ================= WiFi + MQTT =================
const char* WIFI_SSID = "FMS";
const char* WIFI_PASS = "FMS12345";

const char* MQTT_HOST = "192.168.3.100";   // Mosquitto broker
const int   MQTT_PORT = 1883;

WiFiClient espClient;
PubSubClient mqtt(espClient);

// Sparkplug topics (samme format som emulator)
const char* GROUP  = "plantA";
const char* DEVICE = "olimex-device";

String TOP_DBIRTH = String("spBv1.0/") + GROUP + "/DBIRTH/" + DEVICE;
String TOP_DDATA  = String("spBv1.0/") + GROUP + "/DDATA/"  + DEVICE;
String TOP_DDEATH = String("spBv1.0/") + GROUP + "/DDEATH/" + DEVICE;


// ================= RS485 CONTROL =================
void preTransmission() {
  digitalWrite(MAX485_RE_NEG, HIGH);
  digitalWrite(MAX485_DE, HIGH);
}

void postTransmission() {
  digitalWrite(MAX485_RE_NEG, LOW);
  digitalWrite(MAX485_DE, LOW);
}

// ================= GENERIC MODBUS =================
bool readRegistersBlock(uint16_t regs[11]) {
  uint8_t result = modbus.readInputRegisters(10, 11);
  if (result != modbus.ku8MBSuccess) return false;
  for (int i = 0; i < 11; i++) {
    regs[i] = modbus.getResponseBuffer(i);
  }
  return true;
}

// Extract values
float getTemperature(uint16_t regs[11]) { return regs[9] / 10.0f; }
float getPressure(uint16_t regs[11])    { return regs[3] / 10.0f; }
float getAirFlow(uint16_t regs[11])     { return regs[5]; }

// ================= SPECIALIZED FUNCTIONS =================
void fanStart() {
  Serial.println("Starter ventilation (fanStart)"); 
  uint8_t result = modbus.writeSingleRegister(367, 3); // Skriv til holding register 367 med værdien 0 for sluk og 3 for start.
  if (result == modbus.ku8MBSuccess) {
    Serial.println("Ventilation startet: register skriv ok");
  } else {
    Serial.print("Ventilation start fejlede, modbus fejlkode: ");
    Serial.println(result);
  }
}

// ================= BUILD PAYLOAD (fallback) =================
String makePayload(float t, float p, float rpm) {
  String s = "temp=" + String(t, 1)
           + ",tryk=" + String(p, 1)
           + ",rpm="  + String((int)rpm);
  return s;
}

// ================= MQTT CONNECT =================
void mqttReconnect() {
  Serial.println("MQTT: Forsøger at forbinde til broker...");
  while (!mqtt.connected()) {
    Serial.println("MQTT: Ikke forbundet, prøver igen...");
    if (mqtt.connect("olimex-client", NULL, NULL, 
                     TOP_DDEATH.c_str(), 1, false, "DDEATH")) {

      mqtt.publish(TOP_DBIRTH.c_str(), "DBIRTH", false);
      Serial.println("MQTT: DBIRTH sendt");
      Serial.println("MQTT: Forbundet til broker!");
    }
    delay(1000);
  }
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

  // Start ventilation kort efter Modbus init
  fanStart();
  delay(300);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.println("Forbinder til WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(200);
  }
  Serial.println();
  Serial.print("WiFi forbundet. IP: ");
  Serial.println(WiFi.localIP());

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
}

// ================= LOOP =================
void loop() {
  if (!mqtt.connected()) mqttReconnect();
  mqtt.loop();

  uint16_t regs[11] = {0};
  if (!readRegistersBlock(regs)) {
    delay(2000);
    return;
  }

  float t  = getTemperature(regs);
  float p  = getPressure(regs);
  float af = getAirFlow(regs);

  String payload = makePayload(t, p, af);
  Serial.print("Sender payload: ");
  Serial.println(payload);
  mqtt.publish(TOP_DDATA.c_str(), payload.c_str(), false);
  Serial.println("Payload sendt til MQTT broker.");

  delay(2000);
}
