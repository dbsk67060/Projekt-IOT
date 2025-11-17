from flask import Flask, render_template, request, redirect, url_for, session, jsonify
from datetime import datetime
import os
import docker
import requests  # NYT

ESP32_IP = "http://xxx.xxx.xxx.xxx" # IP adresse på ESP32 - ændre til rigtige

app = Flask(__name__)
app.secret_key = os.getenv("SECRET_KEY", "supersecretkey")

# Docker client
docker_client = docker.DockerClient(base_url="unix://var/run/docker.sock")

# FastAPI URL – ændr hvis IP/hostname er anderledes
FASTAPI_URL = os.getenv("FASTAPI_URL", "http://fastapi:80")

# Dummy user
USER = {"username": "admin", "password": "password"}


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

#------------------------
#   TEST - slet når der er testet faerdig
#-----------------------
@app.route("/test-fastapi")
def test_fastapi():
    import requests
    r = requests.get("http://fastapi:80/sensors")
    return r.json()
#------------------------
#    Ventilator start/Stop
#------------------------
@app.route("/fan/start", methods=["POST"])
def fan_start():
    r = requests.get(f"{ESP32_IP}/fan/start")
    return jsonify({"status": "ok"})

@app.route("/fan/stop", methods=["POST"])
def fan_stop():
    r = requests.get(f"{ESP32_IP}/fan/stop")
    return jsonify({"status": "ok"})
# -----------------------
# DASHBOARD – viser graf
# -----------------------
@app.route("/dashboard")
def dashboard():
    if "user" not in session:
        return redirect(url_for("login"))

    try:
        # Hent data fra FastAPI (som igen henter fra QuestDB)
        r = requests.get(f"{FASTAPI_URL}/sensors", timeout=5)
        r.raise_for_status()
        data = r.json().get("rows", [])
    except Exception as e:
        data = []
        print(f"Fejl ved hentning af sensordata: {e}")

    # Tilpas felterne her så de matcher din QuestDB-tabel
    labels = [datetime.fromisoformat(row.get("timestamp")).strftime("%d-%m-%Y %H:%M") for row in data]
    values = [row.get("temperature") for row in data]

    return render_template("dashboard.html", data=data, user=session["user"])

# -----------------------
# LOGOUT
# -----------------------
@app.route("/logout")
def logout():
    session.clear()
    return redirect(url_for("login"))

# -----------------------
# HEALTHCHECK ENDPOINT
# -----------------------
@app.route("/health")
def health():
    return {"status": "ok"}, 200


# -----------------------
# HEALTH DATA API (Docker)
# -----------------------
@app.route("/health-data")
def health_data():
    if "user" not in session:
        return jsonify({"error": "unauthorized"}), 401

    containers_info = []
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
# NYT ENDPOINT – Flask-API til frontend
# -----------------------
@app.route("/sensor-data")
def sensor_data():
    if "user" not in session:
        return jsonify({"error": "unauthorized"}), 401
    try:
        r = requests.get(f"{FASTAPI_URL}/sensors", timeout=5)
        r.raise_for_status()
        data = r.json()
        return jsonify(data)
    except Exception as e:
        return jsonify({"error": str(e)}), 500


# -----------------------
# HEALTH DASHBOARD
# -----------------------
@app.route("/health-dashboard")
def health_dashboard():
    if "user" not in session:
        return redirect(url_for("login"))
    return render_template("health_dashboard.html", user=session["user"])


# -----------------------
# START APP
# -----------------------
if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)