/*
  ESP32 Sparkplug B (minimal) emulator for testing DB/API
  Target: ESP32 (e.g., OLIMEX ESP32-EVB/POE)
  Toolchain: Arduino-ESP32 2.x

  What this does
  -------------
  - Connects to WiFi + MQTT broker
  - Implements a *minimal* Sparkplug B edge node with one device
  - Publishes NBIRTH (node birth)
  - Publishes DBIRTH for a single device ("oil-sensor-1")
  - Periodically publishes DDATA metrics (turbidity, status)
  - Supports NCMD/DCMD topics (basic: forces next publish)
  - Sets MQTT LWT on NDEATH

  Notes
  -----
  - This uses nanopb-generated structs from Sparkplug B .proto files.
  - You must generate "sparkplug_b.pb.h/.c" (and dependencies) with nanopb and add to the project.
  - This is a *minimal subset* of the spec for emulator/testing purposes.

  Dependencies (Arduino Library Manager)
  -------------------------------------
  - PubSubClient by Nick O'Leary
  - ArduinoJson (optional - only for logs)
  - nanopb (install manually by dropping generated files into the project src/)

  Protobuf generation (run on your dev machine)
  --------------------------------------------
  # 1) Install protoc and nanopb generator
  #    https://jpa.kapsi.fi/nanopb/download/
  # 2) Get Sparkplug B proto (e.g., from Eclipse Tahu repo): sparkplug_b.proto
  # 3) Generate nanopb sources:
  #    protoc --nanopb_out=. sparkplug_b.proto
  # This produces: sparkplug_b.pb.h and sparkplug_b.pb.c (and maybe others)
  # Copy them into your Arduino sketch folder (or a library folder).

*/

#include <WiFi.h>
#include <PubSubClient.h>

// nanopb
extern "C" {
  #include "pb.h"
  #include "pb_encode.h"
  #include "sparkplug_b.pb.h"  // Generated from sparkplug_b.proto
}

/*********** USER CONFIG ***********/
const char* WIFI_SSID     = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASS";

const char* MQTT_HOST = "192.168.1.10"; // your broker (e.g., Mosquitto on RPi/Server)
const uint16_t MQTT_PORT = 1883;

// Sparkplug group/node/device identifiers
const char* SP_GROUP = "testGroup";
const char* SP_NODE  = "esp32-olimex";  // edge node name
const char* SP_DEVICE = "oil-sensor-1"; // device under node

// Publish period (ms)
const unsigned long PUBLISH_MS = 5000;
/***********************************/

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// Sparkplug namespace/version
static const char* SP_NS = "spBv1.0"; // Topic namespace

// Sequence numbers
uint64_t seq = 0;      // general sequence for metrics
uint64_t bdSeq = 0;    // birth/death sequence (Sparkplug field)

unsigned long lastPublish = 0;
bool deviceBirthSent = false;

// Temp: we simulate a turbidity reading
float fakeTurbidity = 0.0f;

/********* Helpers: topic builders *********/
String tNBIRTH() {  return String(SP_NS) + "/NBIRTH/" + SP_GROUP + "/" + SP_NODE; }
String tNDEATH() {  return String(SP_NS) + "/NDEATH/" + SP_GROUP + "/" + SP_NODE; }
String tNDATA()  {  return String(SP_NS) + "/NDATA/"  + SP_GROUP + "/" + SP_NODE; }
String tNCMD()   {  return String(SP_NS) + "/NCMD/"   + SP_GROUP + "/" + SP_NODE; }

String tDBIRTH() {  return String(SP_NS) + "/DBIRTH/" + SP_GROUP + "/" + SP_NODE + "/" + SP_DEVICE; }
String tDDATA()  {  return String(SP_NS) + "/DDATA/"  + SP_GROUP + "/" + SP_NODE + "/" + SP_DEVICE; }
String tDCMD()   {  return String(SP_NS) + "/DCMD/"   + SP_GROUP + "/" + SP_NODE + "/" + SP_DEVICE; }

/********* nanopb encode helpers (minimal) *********/
// Build a SparkplugB Payload with one or more metrics.
// We only use a tiny subset of fields here.

// Create a metric (double/float)
bool addMetricFloat(Payload *pl, const char* name, float value) {
  if (pl->metrics_count >= Payload_metrics_max_count) return false; // depends on your .options during generation
  Payload_Metric *m = &pl->metrics[pl->metrics_count++];
  *m = Payload_Metric_init_default;
  m->name.funcs.encode = &pb_encode_string;
  m->name.arg = (void*)name;
  m->datatype = Payload_Metric_DataType_FLOAT;
  m->which_value = Payload_Metric_float_value_tag;
  m->value.float_value = value;
  return true;
}

// Create a metric (boolean)
bool addMetricBool(Payload *pl, const char* name, bool value) {
  if (pl->metrics_count >= Payload_metrics_max_count) return false;
  Payload_Metric *m = &pl->metrics[pl->metrics_count++];
  *m = Payload_Metric_init_default;
  m->name.funcs.encode = &pb_encode_string;
  m->name.arg = (void*)name;
  m->datatype = Payload_Metric_DataType_BOOLEAN;
  m->which_value = Payload_Metric_boolean_value_tag;
  m->value.boolean_value = value;
  return true;
}

// Create a metric (string)
bool addMetricString(Payload *pl, const char* name, const char* value) {
  if (pl->metrics_count >= Payload_metrics_max_count) return false;
  Payload_Metric *m = &pl->metrics[pl->metrics_count++];
  *m = Payload_Metric_init_default;
  m->name.funcs.encode = &pb_encode_string;
  m->name.arg = (void*)name;
  m->datatype = Payload_Metric_DataType_STRING;
  m->which_value = Payload_Metric_string_value_tag;
  m->value.string_value.funcs.encode = &pb_encode_string;
  m->value.string_value.arg = (void*)value;
  return true;
}

// Build a payload with bdSeq + seq and encode to buffer
size_t buildPayload(uint8_t *out, size_t outSize, bool includeDeviceMeta, bool isBirth) {
  Payload pl = Payload_init_default;
  pl.has_timestamp = false; // optional
  pl.has_seq = true;
  pl.seq = seq++;

  // Sparkplug birth/death sequence metric is required in birth/death
  if (isBirth) {
    addMetricFloat(&pl, "bdSeq", (float)bdSeq);
  }

  if (includeDeviceMeta && isBirth) {
    // Typical birth metrics: properties and meta
    addMetricString(&pl, "Device Control/Type", "ESP32-Olimex Emulator");
    addMetricBool(&pl,   "Device Control/Rebirth", false);
    addMetricString(&pl, "Node Control/Schema", "spBv1.0");
  }

  // Our example live metrics
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
  // For simplicity, any NCMD/DCMD triggers immediate publish of next DDATA
  if (String(topic) == tNCMD() || String(topic) == tDCMD()) {
    lastPublish = 0; // force publish ASAP
  }
}

bool publishBirth() {
  uint8_t buf[512];

  // NBIRTH
  {
    bdSeq++; // increment on each birth
    size_t n = buildPayload(buf, sizeof(buf), /*includeDeviceMeta=*/true, /*isBirth=*/true);
    if (!n) return false;
    bool ok = mqtt.publish(tNBIRTH().c_str(), buf, n, true);
    Serial.println(ok ? "NBIRTH sent" : "NBIRTH failed");
    if (!ok) return false;
  }

  // DBIRTH for our single device
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
  // wiggle fake value
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

  // Set LWT to NDEATH with empty payload
  String ndeath = tNDEATH();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(1024);

  // Subscribe to command topics when connected
  while (!mqtt.connected()) {
    String clientId = String("spb-emu-") + String((uint32_t)ESP.getEfuseMac(), HEX);
    Serial.printf("Connecting MQTT as %s...\n", clientId.c_str());

    if (mqtt.connect(clientId.c_str(), NULL, NULL, ndeath.c_str(), 1, true, "")) {
      mqtt.subscribe(tNCMD().c_str());
      mqtt.subscribe(tDCMD().c_str());
      Serial.println("MQTT connected");
      publishBirth();
    } else {
      Serial.printf("MQTT failed rc=%d, retrying in 2s...\n", mqtt.state());
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Sparkplug B emulator starting...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to WiFi %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) { Serial.print("."); delay(500); }
  Serial.printf("\nWiFi OK: %s\n", WiFi.localIP().toString().c_str());

  mqtt.setKeepAlive(30);
  ensureConnected();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    // attempt reconnect
  }
  ensureConnected();
  mqtt.loop();

  unsigned long now = millis();
  if (deviceBirthSent && (now - lastPublish >= PUBLISH_MS)) {
    lastPublish = now;
    publishData();
  }
}

/**** Minimal string encoder for nanopb fields ****/
// Helper to encode const char* via nanopb (used above)
bool pb_encode_string(pb_ostream_t *stream, const pb_field_t *field, void * const *arg) {
  const char *str = (const char*)(*arg);
  if (!pb_encode_tag_for_field(stream, field)) return false;
  return pb_encode_string(stream, (const uint8_t*)str, strlen(str));
}
