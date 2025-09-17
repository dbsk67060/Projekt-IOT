/*
  ESP32 Sparkplug B (minimal) emulator for testing DB/API
  Target: ESP32 (e.g., OLIMEX ESP32-EVB/POE) — with optional Ethernet + ZeroTier gateway support
  Toolchain: Arduino-ESP32 2.x

  What this does
  -------------
  - Connects to Ethernet (OLIMEX ESP32-POE) and/or Wi‑Fi as fallback
  - Implements a *minimal* Sparkplug B edge node with one device
  - Publishes NBIRTH (node birth) and DBIRTH (device birth)
  - Periodically publishes DDATA metrics (turbidity, status)
  - Listens on NCMD/DCMD (forces next publish)
  - Sets MQTT LWT on NDEATH

  ZeroTier gateway model (important)
  ----------------------------------
  ESP32 kan ikke selv køre en ZeroTier-klient. I stedet forbinder vi til en broker
  på jeres ZeroTier-net via en router/gateway eller en lille edge (fx Raspberry Pi),
  som *er* medlem af ZeroTier og annoncerer route eller NAT'er trafikken.
  Sæt ZT_BROKER_IP til brokerens ZeroTier-IP. Hvis forbindelsen fejler, falder vi
  tilbage til en lokal/alternativ broker (MQTT_HOST/MQTT_PORT).

  Dependencies (Arduino Library Manager)
  -------------------------------------
  - PubSubClient by Nick O'Leary
  - nanopb (drop generated files into project)

  Protobuf generation
  -------------------
    protoc --nanopb_out=. sparkplug_b.proto

*/

#include <WiFi.h>
#include <ETH.h>
#include <PubSubClient.h>

extern "C" {
  #include "pb.h"
  #include "pb_encode.h"
  #include "sparkplug_b.pb.h"  // Generated from sparkplug_b.proto
}

/*********** USER CONFIG ***********/
// Wi‑Fi fallback (bruges hvis Ethernet ikke får IP)
const char* WIFI_SSID     = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASS";

// === ZeroTier gateway model ===
// Brokerens ZeroTier-IP (kræver at jeres gateway/edge er i samme ZT-net og ruter dertil)
const bool     USE_ZT        = true;             // prøv ZeroTier-broker først
const char*    ZT_BROKER_IP  = "10.147.0.42";   // eksempel: brokerens ZT-IP
const uint16_t ZT_MQTT_PORT  = 1883;             // 1883 (eller 8883 for TLS)

// Lokal/alternativ broker (fallback)
const char*    MQTT_HOST     = "192.168.1.10";  // lokal Mosquitto
const uint16_t MQTT_PORT     = 1883;

// Sparkplug group/node/device identifiers
const char* SP_GROUP  = "testGroup";
const char* SP_NODE   = "esp32-olimex";
const char* SP_DEVICE = "oil-sensor-1";

// Publish period (ms)
const unsigned long PUBLISH_MS = 5000;

// === OLIMEX ESP32-POE Ethernet pins (justér hvis nødvendigt) ===
#define ETH_PHY_ADDR      0
#define ETH_PHY_TYPE      ETH_PHY_LAN8720
#define ETH_CLK_MODE      ETH_CLOCK_GPIO17_OUT
#define ETH_POWER_PIN     12
#define ETH_MDC_PIN       23
#define ETH_MDIO_PIN      18
/***********************************/

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

static const char* SP_NS = "spBv1.0"; // Topic namespace

uint64_t seq = 0;      // general sequence for metrics
uint64_t bdSeq = 0;    // birth/death sequence

unsigned long lastPublish = 0;
bool deviceBirthSent = false;

// Aktiv broker endpoint (sættes ved connect)
IPAddress activeBrokerIP;
uint16_t  activeBrokerPort = 0;

float fakeTurbidity = 0.0f; // simulated value

/********* Helpers: topic builders *********/
String tNBIRTH() {  return String(SP_NS) + "/NBIRTH/" + SP_GROUP + "/" + SP_NODE; }
String tNDEATH() {  return String(SP_NS) + "/NDEATH/" + SP_GROUP + "/" + SP_NODE; }
String tNDATA()  {  return String(SP_NS) + "/NDATA/"  + SP_GROUP + "/" + SP_NODE; }
String tNCMD()   {  return String(SP_NS) + "/NCMD/"   + SP_GROUP + "/" + SP_NODE; }

String tDBIRTH() {  return String(SP_NS) + "/DBIRTH/" + SP_GROUP + "/" + SP_NODE + "/" + SP_DEVICE; }
String tDDATA()  {  return String(SP_NS) + "/DDATA/"  + SP_GROUP + "/" + SP_NODE + "/" + SP_DEVICE; }
String tDCMD()   {  return String(SP_NS) + "/DCMD/"   + SP_GROUP + "/" + SP_NODE + "/" + SP_DEVICE; }

/********* nanopb encode helpers (minimal) *********/
// Minimal helper to encode const char* for nanopb string fields
bool pb_encode_string_field(pb_ostream_t *stream, const pb_field_t *field, void * const *arg) {
  const char *str = (const char*)(*arg);
  if (!pb_encode_tag_for_field(stream, field)) return false;
  return pb_encode_string(stream, (const uint8_t*)str, strlen(str));
}

bool addMetricFloat(Payload *pl, const char* name, float value) {
  if (pl->metrics_count >= Payload_metrics_max_count) return false;
  Payload_Metric *m = &pl->metrics[pl->metrics_count++];
  *m = Payload_Metric_init_default;
  m->name.funcs.encode = &pb_encode_string_field;
  m->name.arg = (void*)name;
  m->datatype = Payload_Metric_DataType_FLOAT;
  m->which_value = Payload_Metric_float_value_tag;
  m->value.float_value = value;
  return true;
}

bool addMetricBool(Payload *pl, const char* name, bool value) {
  if (pl->metrics_count >= Payload_metrics_max_count) return false;
  Payload_Metric *m = &pl->metrics[pl->metrics_count++];
  *m = Payload_Metric_init_default;
  m->name.funcs.encode = &pb_encode_string_field;
  m->name.arg = (void*)name;
  m->datatype = Payload_Metric_DataType_BOOLEAN;
  m->which_value = Payload_Metric_boolean_value_tag;
  m->value.boolean_value = value;
  return true;
}

bool addMetricString(Payload *pl, const char* name, const char* value) {
  if (pl->metrics_count >= Payload_metrics_max_count) return false;
  Payload_Metric *m = &pl->metrics[pl->metrics_count++];
  *m = Payload_Metric_init_default;
  m->name.funcs.encode = &pb_encode_string_field;
  m->name.arg = (void*)name;
  m->datatype = Payload_Metric_DataType_STRING;
  m->which_value = Payload_Metric_string_value_tag;
  m->value.string_value.funcs.encode = &pb_encode_string_field;
  m->value.string_value.arg = (void*)value;
  return true;
}

size_t buildPayload(uint8_t *out, size_t outSize, bool includeDeviceMeta, bool isBirth) {
  Payload pl = Payload_init_default;
  pl.has_seq = true;
  pl.seq = seq++;

  if (isBirth) {
    addMetricFloat(&pl, "bdSeq", (float)bdSeq);
  }

  if (includeDeviceMeta && isBirth) {
    addMetricString(&pl, "Device Control/Type", "ESP32-Olimex Emulator");
    addMetricBool(&pl,   "Device Control/Rebirth", false);
    addMetricString(&pl, "Node Control/Schema", "spBv1.0");
  }

  addMetricFloat(&pl, "oil_turbidity", fakeTurbidity);
  addMetricString(&pl, "status", fakeTurbidity > 50.0 ? "ALERT" : "OK");

  pb_ostream_t stream = pb_ostream_from_buffer(out, outSize);
  if (!pb_encode(&stream, Payload_fields, &pl)) {
    Serial.print("pb_encode failed: "); Serial.println(PB_GET_ERROR(&stream));
    return 0;
  }
  return stream.bytes_written;
}

/********* MQTT *********/
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("[MQTT] %s (%u bytes)\n", topic, length);
  if (String(topic) == tNCMD() || String(topic) == tDCMD()) {
    lastPublish = 0; // force publish ASAP
  }
}

bool publishBirth() {
  uint8_t buf[512];

  // NBIRTH
  {
    bdSeq++;
    size_t n = buildPayload(buf, sizeof(buf), /*includeDeviceMeta=*/true, /*isBirth=*/true);
    if (!n) return false;
    bool ok = mqtt.publish(tNBIRTH().c_str(), buf, n, true);
    Serial.println(ok ? "NBIRTH sent" : "NBIRTH failed");
    if (!ok) return false;
  }

  // DBIRTH
  {
    size_t n = buildPayload(buf, sizeof(buf), /*includeDeviceMeta=*/true, /*isBirth=*/true);
    if (!n) return false;
    bool ok = mqtt.publish(tDBIRTH().c_str(), buf, n, true);
    Serial.println(ok ? "DBIRTH sent" : "DBIRTH failed");
    if (!ok) return false;
  }

  deviceBirthSent = true;
  return true;
}

bool publishData() {
  uint8_t buf[256];
  fakeTurbidity += 7.3f;
  if (fakeTurbidity > 120.0f) fakeTurbidity = 5.0f;

  size_t n = buildPayload(buf, sizeof(buf), /*includeDeviceMeta=*/false, /*isBirth=*/false);
  if (!n) return false;
  bool ok = mqtt.publish(tDDATA().c_str(), buf, n, false);
  Serial.println(ok ? "DDATA sent" : "DDATA failed");
  return ok;
}

void ensureConnected() {
  if (mqtt.connected()) return;

  // Vælg broker (prøv ZeroTier-IP først, hvis slået til)
  if (USE_ZT && activeBrokerIP == IPAddress(0,0,0,0)) {
    IPAddress ip;
    if (ip.fromString(ZT_BROKER_IP)) {
      activeBrokerIP = ip;
      activeBrokerPort = ZT_MQTT_PORT;
    }
  }
  if (!USE_ZT || activeBrokerIP == IPAddress(0,0,0,0)) {
    IPAddress ip;
    if (ip.fromString(MQTT_HOST)) {
      activeBrokerIP = ip;
    } else {
      WiFi.hostByName(MQTT_HOST, activeBrokerIP);
    }
    activeBrokerPort = MQTT_PORT;
  }

  String ndeath = tNDEATH();
  mqtt.setServer(activeBrokerIP, activeBrokerPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(1024);

  while (!mqtt.connected()) {
    String clientId = String("spb-emu-") + String((uint32_t)ESP.getEfuseMac(), HEX);
    Serial.printf("Connecting MQTT %s:%u as %s...\n",
                  activeBrokerIP.toString().c_str(), activeBrokerPort, clientId.c_str());

    if (mqtt.connect(clientId.c_str(), NULL, NULL, ndeath.c_str(), 1, true, "")) {
      mqtt.subscribe(tNCMD().c_str());
      mqtt.subscribe(tDCMD().c_str());
      Serial.println("MQTT connected");
      publishBirth();
    } else {
      Serial.printf("MQTT failed rc=%d, switching/fallback in 3s...\n", mqtt.state());
      // Hvis vi forsøgte ZT-broker og det fejlede, fald tilbage til lokal broker
      if (USE_ZT && activeBrokerIP.toString() == String(ZT_BROKER_IP)) {
        IPAddress ip2;
        if (ip2.fromString(MQTT_HOST)) {
          activeBrokerIP = ip2;
        } else {
          WiFi.hostByName(MQTT_HOST, activeBrokerIP);
        }
        activeBrokerPort = MQTT_PORT;
        mqtt.setServer(activeBrokerIP, activeBrokerPort);
        Serial.println("Falling back to local MQTT broker");
      }
      delay(3000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Sparkplug B emulator starting...");

  // --- Ethernet først (OLIMEX ESP32-POE) ---
  bool ethStarted = false;
#if defined(ETH_PHY_TYPE)
  Serial.println("Bringing up ETH...");
  pinMode(ETH_POWER_PIN, OUTPUT);
  digitalWrite(ETH_POWER_PIN, HIGH);
  delay(50);
  if (ETH.begin(ETH_PHY_ADDR, ETH_PHY_TYPE, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_CLK_MODE)) {
    Serial.println("ETH.begin() ok, waiting for link/IP...");
    uint32_t t0 = millis();
    while (ETH.linkUp() == false && millis() - t0 < 8000) { delay(100); }
    t0 = millis();
    while (ETH.localIP() == IPAddress(0,0,0,0) && millis() - t0 < 8000) { delay(100); }
    if (ETH.localIP() != IPAddress(0,0,0,0)) {
      ethStarted = true;
      Serial.printf("ETH OK: %s\n", ETH.localIP().toString().c_str());
    } else {
      Serial.println("ETH no IP – will try WiFi");
    }
  } else {
    Serial.println("ETH.begin() failed – will try WiFi");
  }
#endif

  // --- Wi‑Fi fallback ---
  if (!ethStarted && WIFI_SSID && strlen(WIFI_SSID)) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("Connecting to WiFi %s", WIFI_SSID);
    while (WiFi.status() != WL_CONNECTED) { Serial.print("."); delay(500); }
    Serial.printf("\nWiFi OK: %s\n", WiFi.localIP().toString().c_str());
  }

  mqtt.setKeepAlive(30);
  ensureConnected();
}

void loop() {
  ensureConnected();
  mqtt.loop();

  unsigned long now = millis();
  if (deviceBirthSent && (now - lastPublish >= PUBLISH_MS)) {
    lastPublish = now;
    publishData();
  }
}
