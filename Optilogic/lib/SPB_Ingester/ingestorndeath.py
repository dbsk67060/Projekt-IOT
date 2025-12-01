import os
import time
import random
import logging
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer  # NEW

import paho.mqtt.client as mqtt

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")

# Try to import Sparkplug B proto if it exists
try:
    import sparkplug_b_pb2 as spb  # normally generated from sparkplug_b.proto
    SPB_AVAILABLE = True
    logging.info("sparkplug_b_pb2 imported successfully, SPB encoding enabled.")
except ImportError:
    spb = None  # type: ignore
    SPB_AVAILABLE = False
    logging.warning(
        "sparkplug_b_pb2 not found, SPB encoding disabled. "
        "Will publish simple text payloads instead of real Sparkplug B."
    )

# Miljø – match your docker-compose (MQTT_HOST=mqtt)
MQTT_HOST   = os.getenv("MQTT_HOST", "mqtt")
MQTT_PORT   = int(os.getenv("MQTT_PORT", "1883"))
GROUP       = os.getenv("SPB_GROUP", "plantA")
DEVICE      = os.getenv("SPB_DEVICE", "test-device")

TOP_DBIRTH  = f"spBv1.0/{GROUP}/DBIRTH/{DEVICE}"
TOP_DDATA   = f"spBv1.0/{GROUP}/DDATA/{DEVICE}"
TOP_DDEATH  = f"spBv1.0/{GROUP}/DDEATH/{DEVICE}"  # NY


def make_spb_payload(temp, tryk, rpm) -> bytes:
    """Ægte Sparkplug B payload (hvis sparkplug_b_pb2 findes)."""
    p = spb.Payload()
    m = p.metrics.add(); m.name = "temp"; m.double_value = float(temp)
    m = p.metrics.add(); m.name = "tryk"; m.double_value = float(tryk)
    m = p.metrics.add(); m.name = "rpm";  m.long_value   = int(rpm)
    return p.SerializeToString()


def make_fallback_payload(temp, tryk, rpm) -> bytes:
    """Fallback payload hvis vi ikke har Sparkplug – bare noget læsbart tekst."""
    text = f"temp={temp:.2f},tryk={tryk:.2f}, rpm={rpm}"
    return text.encode("utf-8")


def make_payload(temp, tryk, rpm) -> bytes:
    """Vælg SPB eller fallback alt efter om sparkplug_b_pb2 findes."""
    if SPB_AVAILABLE:
        return make_spb_payload(temp, tryk, rpm)
    else:
        return make_fallback_payload(temp, tryk, rpm)


def make_death_payload() -> bytes:
    """Payload til DDEATH – meget simpel."""
    if SPB_AVAILABLE:
        p = spb.Payload()
        # Man kan sætte bdSeq og evt. metrics her hvis man vil,
        # men en tom payload er ofte nok til demo/emulator.
        return p.SerializeToString()
    else:
        return b"DDEATH"


# -------------- SIMPLE HTTP HEALTH SERVER (NEW) --------------

HEALTH_PORT = int(os.getenv("SPB_HEALTH_PORT", "8001"))  # match docker-compose


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

    # Silence default logging to stderr
    def log_message(self, format, *args):
        return


def start_health_server():
    server = HTTPServer(("", HEALTH_PORT), HealthHandler)
    logging.info("Starting SPB emulator health server on port %d", HEALTH_PORT)
    server.serve_forever()


def main():
    # Start health server in background thread
    threading.Thread(target=start_health_server, daemon=True).start()

    logging.info(
        "Starter SPB emulator -> MQTT %s:%d group=%s device=%s",
        MQTT_HOST, MQTT_PORT, GROUP, DEVICE,
    )
    if not SPB_AVAILABLE:
        logging.warning(
            "sparkplug_b_pb2 mangler – der sendes IKKE ægte Sparkplug B, "
            "kun simple tekstpayloads."
        )

    # MQTT klient
    c = mqtt.Client(client_id="optilogic-spb-emulator", clean_session=True)

    # Sæt Last Will til DDEATH FØR connect
    ddeath_payload = make_death_payload()
    c.will_set(TOP_DDEATH, ddeath_payload, qos=1, retain=False)

    c.connect(MQTT_HOST, MQTT_PORT, keepalive=30)
    c.loop_start()

    # Send DBIRTH én gang
    c.publish(TOP_DBIRTH, make_payload(22.5, 2.2, 1000), qos=1, retain=False)
    logging.info("Sent DBIRTH -> %s", TOP_DBIRTH)

    try:
        while True:
            temp = 22.0 + random.random() * 3.0
            tryk = 2.0 + random.random() * 0.5
            rpm  = 900 + int(random.random() * 300)

            c.publish(TOP_DDATA, make_payload(temp, tryk, rpm), qos=1, retain=False)
            logging.info(
                "Sent DDATA: temp=%.2f tryk=%.2f rpm=%d ",
                temp, tryk, rpm,
            )
            time.sleep(5)
    except KeyboardInterrupt:
        pass
    finally:
        # Eksplicit DDEATH ved pænt shutdown
        logging.info("Sender DDEATH -> %s", TOP_DDEATH)
        c.publish(TOP_DDEATH, make_death_payload(), qos=1, retain=False)
        # Lille pause så den når at blive sendt
        time.sleep(1)
        c.loop_stop()
        c.disconnect()


if __name__ == "__main__":
    main()
sk@Casaos:/DATA/AppData/spb_emulator$ cd ..
sk@Casaos:/DATA/AppData$ cd spb_ingestor/
sk@Casaos:/DATA/AppData/spb_ingestor$ ls
core.6708  main.py  requirements.txt
sk@Casaos:/DATA/AppData/spb_ingestor$ cat main.py
import os
import logging
from typing import Dict, Any
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from datetime import datetime

import paho.mqtt.client as mqtt
import psycopg2

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")

# ---------------------------------------------------------
# Try to import Sparkplug B protobuf
# ---------------------------------------------------------
try:
    import sparkplug_b_pb2 as spb  # normally generated from sparkplug_b.proto
    SPB_AVAILABLE = True
    logging.info("sparkplug_b_pb2 imported successfully, SPB decoding enabled.")
except ImportError:
    spb = None  # type: ignore
    SPB_AVAILABLE = False
    logging.warning(
        "sparkplug_b_pb2 not found, SPB decoding disabled. "
        "Will use fallback text decoder for payloads."
    )

# ---------------------------------------------------------
# Environment variables
# ---------------------------------------------------------
MQTT_HOST = os.getenv("MQTT_HOST", "mqtt")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
SPB_GROUP = os.getenv("SPB_GROUP", "plantA")
QDB_ILP_HOST = os.getenv("QDB_ILP_HOST", "questdb")
QDB_ILP_PORT = int(os.getenv("QDB_ILP_PORT", "8812"))  # PostgreSQL wire port
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
    """
    spBv1.0/<group>/<type>/<device>
    """
    p = topic.split("/")
    return {
        "type":   p[2] if len(p) > 2 else "",
        "device": p[4] if len(p) > 4 else "",
    }

# ---------------------------------------------------------
# Insert metrics into PostgreSQL with timestamp
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

    # Add timestamp as the last column
    columns.append("timestamp")
    values.append(datetime.utcnow())

    col_str = ",".join(columns)
    val_placeholders = ",".join(["%s"] * len(values))
    sql = f"INSERT INTO {TABLE} ({col_str}) VALUES ({val_placeholders})"

    with _pg_conn.cursor() as cur:
        cur.execute(sql, values)

    logging.info("WROTE PG: device=%s %s", device, fields)

# ---------------------------------------------------------
# Decode payload
# ---------------------------------------------------------
def decode_payload(b: bytes) -> Dict[str, Any]:
    if SPB_AVAILABLE:
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
                elif m.HasField("long_value"):
                    out[m.name] = float(m.long_value)
            elif m.name == "rpm":
                if m.HasField("int_value"):
                    out["rpm"] = int(m.int_value)
                elif m.HasField("long_value"):
                    out["rpm"] = int(m.long_value)
        return out

    # Fallback text parsing
    text = b.decode("utf-8", errors="ignore").strip()
    if not text:
        return {}

    out: Dict[str, Any] = {}
    try:
        for part in text.split(","):
            part = part.strip()
            if not part or "=" not in part:
                continue
            k, v = part.split("=", 1)
            k = k.strip()
            v = v.strip()
            if k in ("temp", "tryk"):
                out[k] = float(v)
            elif k == "rpm":
                out["rpm"] = int(v)
    except Exception as e:
        logging.warning("Fallback decode failed for payload '%s': %s", text, e)

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
            logging.debug("No metrics decoded for topic=%s", msg.topic)
    except Exception as e:
        logging.warning("Decode/PG insert failed: %s (topic=%s)", e, msg.topic)

# ----------------- SIMPLE HTTP HEALTH SERVER -----------------
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
    logging.info("Starting ingestor health server on port %d", INGESTOR_HEALTH_PORT)
    server.serve_forever()

# ---------------------------------------------------------
# Main
# ---------------------------------------------------------
def main():
    threading.Thread(target=start_health_server, daemon=True).start()

    logging.info(
        "Starter ingestor -> QuestDB PostgreSQL %s:%d table=%s",
        QDB_ILP_HOST, QDB_ILP_PORT, TABLE,
    )
    if not SPB_AVAILABLE:
        logging.warning(
            "sparkplug_b_pb2 missing – using fallback text-decoder for payloads."
        )

    client = mqtt.Client(client_id="optilogic-spb-ingestor", clean_session=True)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(MQTT_HOST, MQTT_PORT, keepalive=30)
    client.loop_forever()

if __name__ == "__main__":
    main()