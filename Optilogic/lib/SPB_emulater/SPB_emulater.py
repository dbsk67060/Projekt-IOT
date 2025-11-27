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

# Health server config
HEALTH_PORT = int(os.getenv("SPB_HEALTH_PORT", "8001"))  # match docker-compose


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


# -------------- SIMPLE HTTP HEALTH SERVER (NEW) --------------

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

    c = mqtt.Client(client_id="optilogic-spb-emulator", clean_session=True)
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
        c.loop_stop()
        c.disconnect()


if __name__ == "__main__":
    main()