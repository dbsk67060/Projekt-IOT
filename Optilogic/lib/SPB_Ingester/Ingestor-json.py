import os
import logging
from typing import Dict, Any
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from datetime import datetime

import paho.mqtt.client as mqtt
import psycopg2
import json

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")

# ---------------------------------------------------------
# Sparkplug B protobuf
# ---------------------------------------------------------
try:
    import sparkplug_b_pb2 as spb
    SPB_AVAILABLE = True
    logging.info("sparkplug_b_pb2 imported successfully, SPB decoding enabled.")
except ImportError:
    spb = None
    SPB_AVAILABLE = False
    logging.warning("sparkplug_b_pb2 not found, SPB decoding disabled. Using JSON/fallback.")

# ---------------------------------------------------------
# Environment variables
# ---------------------------------------------------------
MQTT_HOST = os.getenv("MQTT_HOST", "mqtt")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
SPB_GROUP = os.getenv("SPB_GROUP", "plantA")
QDB_ILP_HOST = os.getenv("QDB_ILP_HOST", "questdb")
QDB_ILP_PORT = int(os.getenv("QDB_ILP_PORT", "8812"))
TABLE = os.getenv("QDB_TABLE", "sensor_data")
INGESTOR_HEALTH_PORT = int(os.getenv("INGESTOR_HEALTH_PORT", "8002"))

TOPIC_FILTER = f"spBv1.0/{SPB_GROUP}/#"

# ---------------- PostgreSQL connection ------------------
_pg_conn = None

def init_pg_connection():
    global _pg_conn
    _pg_conn = psycopg2.connect(
        host=QDB_ILP_HOST,
        port=QDB_ILP_PORT,
        dbname="qdb",
        user="admin",
        password="quest"
    )
    _pg_conn.autocommit = True
    logging.info("Connected to QuestDB PostgreSQL wire at %s:%d", QDB_ILP_HOST, QDB_ILP_PORT)

# ---------------------------------------------------------
# Topic parsing
# ---------------------------------------------------------
def parse_topic(topic: str) -> Dict[str, str]:
    p = topic.split("/")
    return {
        "type":   p[2] if len(p) > 2 else "",
        "device": p[4] if len(p) > 4 else "",
    }

# ---------------------------------------------------------
# Insert into QuestDB
# ---------------------------------------------------------
def ilp_send(device: str, fields: Dict[str, Any]) -> None:
    global _pg_conn
    if _pg_conn is None:
        init_pg_connection()

    columns = ["device"]
    values = [device]

    for k in ("temp", "tryk", "rpm"):
        if fields.get(k) is not None:
            columns.append(k)
            values.append(fields[k])

    columns.append("timestamp")
    values.append(datetime.utcnow())

    col_str = ",".join(columns)
    placeholders = ",".join(["%s"] * len(values))

    sql = f"INSERT INTO {TABLE} ({col_str}) VALUES ({placeholders})"

    with _pg_conn.cursor() as cur:
        cur.execute(sql, values)

    logging.info("WROTE PG: device=%s %s", device, fields)

# ---------------------------------------------------------
# Decode payload (Sparkplug → JSON → fallback)
# ---------------------------------------------------------
def decode_payload(b: bytes) -> Dict[str, Any]:

    # -----------------------------
    # 1. Sparkplug B protobuf
    # -----------------------------
    if SPB_AVAILABLE:
        try:
            payload = spb.Payload()
            payload.ParseFromString(b)
            out: Dict[str, Any] = {}

            for m in payload.metrics:
                if not m.name:
                    continue
                if m.name in ("temp", "tryk"):
                    if m.HasField("float_value"):
                        out[m.name] = float(m.float_value)
                    elif m.HasField("double_value"):
                        out[m.name] = float(m.double_value)
                    elif m.HasField("int_value"):
                        out[m.name] = float(m.int_value)
                elif m.name == "rpm":
                    if m.HasField("int_value"):
                        out["rpm"] = int(m.int_value)
                    elif m.HasField("long_value"):
                        out["rpm"] = int(m.long_value)

            if out:
                return out

        except Exception:
            pass

    # -----------------------------
    # 2. JSON (from ESP32)
    # -----------------------------
    text = b.decode("utf-8", errors="ignore").strip()

    try:
        j = json.loads(text)
        out = {}
        if "temp" in j: out["temp"] = float(j["temp"])
        if "tryk" in j: out["tryk"] = float(j["tryk"])
        if "rpm"  in j: out["rpm"]  = int(j["rpm"])
        if out:
            return out
    except Exception:
        pass

    # -----------------------------
    # 3. Fallback text "temp=23,tryk=12,rpm=850"
    # -----------------------------
    out: Dict[str, Any] = {}
    try:
        for part in text.split(","):
            if "=" not in part:
                continue
            k, v = part.split("=", 1)
            k = k.strip()
            v = v.strip()
            if k in ("temp", "tryk"):
                out[k] = float(v)
            elif k == "rpm":
                out["rpm"] = int(v)
    except Exception:
        logging.warning("fallback decode failed: %s", text)

    return out

# ---------------------------------------------------------
# MQTT callbacks
# ---------------------------------------------------------
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        logging.info("MQTT connected %s:%d", MQTT_HOST, MQTT_PORT)
        client.subscribe(TOPIC_FILTER, qos=1)
        logging.info("Subscribed: %s", TOPIC_FILTER)
    else:
        logging.error("MQTT connect rc=%s", rc)

def on_message(client, userdata, msg):
    meta = parse_topic(msg.topic)
    if meta["type"] not in ("DBIRTH", "DDATA"):
        return

    try:
        metrics = decode_payload(msg.payload)
        if metrics:
            dev = meta["device"] or "device"
            ilp_send(dev, metrics)
        else:
            logging.debug("No metrics decoded for %s", msg.topic)
    except Exception as e:
        logging.warning("Decode/insert error: %s", e)

# ---------------------------------------------------------
# Health server
# ---------------------------------------------------------
class HealthHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/health":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"ok")
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        return

def start_health_server():
    server = HTTPServer(("", INGESTOR_HEALTH_PORT), HealthHandler)
    logging.info("Starting ingestor health server on %d", INGESTOR_HEALTH_PORT)
    server.serve_forever()

# ---------------------------------------------------------
# Main
# ---------------------------------------------------------
def main():
    threading.Thread(target=start_health_server, daemon=True).start()

    logging.info("Starting ingestor → QuestDB %s:%d table=%s",
                 QDB_ILP_HOST, QDB_ILP_PORT, TABLE)

    client = mqtt.Client(client_id="optilogic-spb-ingestor", clean_session=True)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(MQTT_HOST, MQTT_PORT, keepalive=30)
    client.loop_forever()

if __name__ == "__main__":
    main()
sk@Casaos:/DATA/AppData/spb_ingestor$