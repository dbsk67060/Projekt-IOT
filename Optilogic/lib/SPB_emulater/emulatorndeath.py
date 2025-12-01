import os
import requests
from datetime import datetime
from flask import Flask, render_template, request, redirect, url_for, session, jsonify

ESP32_IP = "http://fake-esp32"  # default for testing

USER = {"username": "admin", "password": "password"}


def create_app(docker_client=None, fastapi_url="http://fake-fastapi"):
    """Factory function to create the Flask app."""
    app = Flask(__name__)
    app.secret_key = os.getenv("SECRET_KEY", "supersecretkey")

    # Save injected dependencies
    app.config["DOCKER_CLIENT"] = docker_client
    app.config["FASTAPI_URL"] = fastapi_url

    # -----------------------
    # LOGIN
    # -----------------------
    @app.route("/", methods=["GET", "POST"])
    def login():
        if request.method == "POST":
            username = request.form.get("username")
            password = request.form.get("password")
            if username == USER["username"] and password == USER["password"]:
                session["user"] = username
                return redirect(url_for("dashboard"))
            return render_template("login.html", error="Invalid credentials")
        return render_template("login.html")

    # -----------------------
    # DASHBOARD
    # -----------------------
    @app.route("/dashboard")
    def dashboard():
        if "user" not in session:
            return redirect(url_for("login"))
        try:
            import requests  # lazy import
            r = requests.get(f"{app.config['FASTAPI_URL']}/sensors", timeout=5)
            r.raise_for_status()
            data = r.json().get("rows", [])
        except Exception:
            data = []
        return render_template("dashboard.html", data=data, user=session["user"])

    # -----------------------
    # FAN CONTROL
    # -----------------------
    @app.route("/fan/start", methods=["POST"])
    def fan_start():
        try:
            import requests  # lazy import
            requests.get(f"{ESP32_IP}/fan/start")
        except Exception:
            pass
        return jsonify({"status": "ok"})

    @app.route("/fan/stop", methods=["POST"])
    def fan_stop():
        try:
            import requests
            requests.get(f"{ESP32_IP}/fan/stop")
        except Exception:
            pass
        return jsonify({"status": "ok"})

    # -----------------------
    # HEALTH
    # -----------------------
    @app.route("/health")
    def health():
        return {"status": "ok"}, 200

    # -----------------------
    # LOGOUT
    # -----------------------
    @app.route("/logout")
    def logout():
        session.clear()
        return redirect(url_for("login"))

    # -----------------------
    # HEALTH DATA (Docker)
    # -----------------------
    @app.route("/health-data")
    def health_data():
        if "user" not in session:
            return jsonify({"error": "unauthorized"}), 401

        containers_info = []
        if docker_client:
            for c in docker_client.containers.list(all=True):
                state = c.attrs.get("State", {})
                health = state.get("Health")
                last_log_output = None
                if health and health.get("Log"):
                    last_entry = health["Log"][-1]
                    last_log_output = last_entry.get("Output")
                containers_info.append({
                    "name": c.name,
                    "status": state.get("Status"),
                    "health": health.get("Status") if health else None,
                    "running": state.get("Running"),
                    "restart_count": state.get("RestartCount"),
                    "health_log": last_log_output,
                })

        return jsonify(containers_info)

    # -----------------------
    # SENSOR DATA (FastAPI)
    # -----------------------
    @app.route("/sensor-data")
    def sensor_data():
        if "user" not in session:
            return jsonify({"error": "unauthorized"}), 401
        try:
            import requests
            r = requests.get(f"{app.config['FASTAPI_URL']}/sensors", timeout=5)
            r.raise_for_status()
            data = r.json()
            return jsonify(data)
        except Exception as e:
            return jsonify({"error": str(e)}), 500

    return app
sk@Casaos:~/unittest/flasktest$ cd ..
sk@Casaos:~/unittest$ cd ..
sk@Casaos:~$ cd ..
sk@Casaos:/home$ cd ..
sk@Casaos:/$ ls
bin     DATA  flasktest   initrd.img.old  loki        mnt   root  srv    tmp  vmlinuz
boot    dev   home        lib             lost+found  opt   run   sys    usr  vmlinuz.old
config  etc   initrd.img  lib64           media       proc  sbin  tests  var
sk@Casaos:/$ cd DATA/
sk@Casaos:/DATA$ ls
AppData  Documents  Downloads  Gallery  Media
sk@Casaos:/DATA$ cd a
-bash: cd: a: No such file or directory
sk@Casaos:/DATA$ cd AppData/
sk@Casaos:/DATA/AppData$ ls
api                 doxygen  grafana  mosquitto  pyscripts  spb-config    spb_ingestor
big-bear-portainer  flask    loki     promtail   questdb    spb_emulator
sk@Casaos:/DATA/AppData$ cd spb_emulator/
sk@Casaos:/DATA/AppData/spb_emulator$ ls
core.1165  core.1676  core.2131  core.2196  pubbackup.py  pub.py  requirements.txt
sk@Casaos:/DATA/AppData/spb_emulator$ cd pub.py
-bash: cd: pub.py: Not a directory
sk@Casaos:/DATA/AppData/spb_emulator$ cat pub
cat: pub: No such file or directory
sk@Casaos:/DATA/AppData/spb_emulator$ cat pub.py
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