# flasktest/tests/test_flask.py  # Filsti og filnavn til Flask-tests
import pytest  # Importerer pytest til test og fixtures
from app import create_app  # Importerer create_app-funktionen fra app
from unittest.mock import patch  # Importerer patch til mocking af funktioner

# -----------------------
# Flask app fixtures
# -----------------------
@pytest.fixture  # Deklarerer en pytest fixture
def app():  # Definerer fixture til Flask app
    app = create_app(docker_client=None, fastapi_url="http://fake-fastapi")  # Opretter app med falske dependencies
    app.config.update({"TESTING": True, "SECRET_KEY": "testkey"})  # Sætter test-konfiguration
    return app  # Returnerer app-instansen

@pytest.fixture  # Deklarerer endnu en fixture
def client(app):  # Fixture som afhænger af app-fixturen
    return app.test_client()  # Returnerer Flask test client

# -----------------------
# LOGIN ROUTES
# -----------------------
def test_login_page_loads(client):  # Tester at login-siden kan loades
    with patch("app.render_template") as mock_render:  # Mocker render_template
        mock_render.return_value = "login page"  # Definerer hvad mocken returnerer
        response = client.get("/")  # Sender GET-request til login-route
        assert response.status_code == 200  # Tjekker at HTTP-status er 200
        assert response.data == b"login page"  # Tjekker at korrekt indhold returneres

def test_login_success_redirects_to_dashboard(client):  # Tester succesfuldt login
    with patch("app.render_template") as mock_render:  # Mocker render_template
        mock_render.return_value = "dashboard page"  # Simulerer dashboard-render
        response = client.post(  # Sender POST-request til login
            "/", data={"username": "admin", "password": "password"}, follow_redirects=True  # Korrekte login-data
        )
        assert response.status_code == 200  # Tjekker statuskode
        assert response.data == b"dashboard page"  # Tjekker at dashboard vises

def test_login_fail_shows_error(client):  # Tester fejlet login
    with patch("app.render_template") as mock_render:  # Mocker render_template
        mock_render.return_value = "login page"  # Forventer login-siden igen
        response = client.post("/", data={"username": "wrong", "password": "wrong"})  # Forkerte credentials
        assert response.data == b"login page"  # Tjekker at login-siden vises igen

# -----------------------
# DASHBOARD ROUTE
# -----------------------
@patch("app.requests.get")  # Mocker requests.get
def test_dashboard_fetches_sensor_data(mock_get, client):  # Tester dashboard med sensor-data
    mock_get.return_value.json.return_value = {  # Definerer fake API-response
        "rows": [{"timestamp": "2025-11-19T12:00:00", "temperature": 25}]
    }
    mock_get.return_value.raise_for_status = lambda: None  # Simulerer ingen HTTP-fejl

    with patch("app.render_template") as mock_render:  # Mocker template-rendering
        mock_render.return_value = "dashboard page"  # Fake dashboard-output
        with client.session_transaction() as sess:  # Åbner session
            sess["user"] = "admin"  # Simulerer logget bruger
        res = client.get("/dashboard")  # Sender GET-request til dashboard
        assert res.status_code == 200  # Tjekker statuskode
        assert res.data == b"dashboard page"  # Tjekker korrekt output

@patch("app.requests.get", side_effect=Exception("API DOWN"))  # Simulerer API-fejl
def test_dashboard_handles_api_failure(_, client):  # Tester fejl-håndtering i dashboard
    with patch("app.render_template") as mock_render:  # Mocker render_template
        mock_render.return_value = "dashboard page"  # Dashboard vises trods fejl
        with client.session_transaction() as sess:  # Åbner session
            sess["user"] = "admin"  # Simulerer logget bruger
        res = client.get("/dashboard")  # Kalder dashboard
        assert res.status_code == 200  # Dashboard loader stadig
        assert res.data == b"dashboard page"  # Tjekker output

def test_dashboard_unauthorized_redirects(client):  # Tester adgang uden login
    res = client.get("/dashboard")  # Kalder dashboard uden session
    assert res.status_code == 302  # Forventer redirect
    assert res.location.endswith("/")  # Tjekker redirect til login

# -----------------------
# kan blæser tændes og slukkes
# -----------------------
@patch("app.requests.get")  # Mocker requests.get
def test_fan_start_stop(mock_get, client):  # Tester start/stop af blæser
    mock_get.return_value.json.return_value = {"status": "ok"}  # Fake API-response
    mock_get.return_value.raise_for_status = lambda: None  # Ingen fejl

    res = client.post("/fan/start")  # Sender POST til start
    assert res.json == {"status": "ok"}  # Tjekker svar
    res = client.post("/fan/stop")  # Sender POST til stop
    assert res.json == {"status": "ok"}  # Tjekker svar

# -----------------------
# HEALTH
# -----------------------
def test_health_returns_ok(client):  # Tester health endpoint
    res = client.get("/health")  # Sender GET-request
    assert res.status_code == 200  # Tjekker statuskode
    assert res.json == {"status": "ok"}  # Tjekker JSON-svar

# -----------------------
# LOGOUT
# -----------------------
def test_logout_clears_session(client):  # Tester logout funktionalitet
    with patch("app.render_template") as mock_render:  # Mocker render_template
        mock_render.return_value = "login page"  # Forventer login-siden
        with client.session_transaction() as sess:  # Åbner session
            sess["user"] = "admin"  # Simulerer logget bruger
        res = client.get("/logout", follow_redirects=True)  # Logger ud
        assert res.status_code == 200  # Tjekker statuskode
        assert res.data == b"login page"  # Tjekker at login-siden vises

# -----------------------
# SENSOR DATA (FastAPI)
# -----------------------
@patch("app.requests.get")  # Mocker requests.get
def test_sensor_data_success(mock_get, client):  # Tester succesfuld sensor-data
    mock_get.return_value.json.return_value = {"rows": []}  # Fake data
    mock_get.return_value.raise_for_status = lambda: None  # Ingen fejl

    with client.session_transaction() as sess:  # Åbner session
        sess["user"] = "admin"  # Simulerer login
    res = client.get("/sensor-data")  # Kalder endpoint
    assert res.status_code == 200  # Tjekker status
    assert "rows" in res.json  # Tjekker indhold

@patch("app.requests.get", side_effect=Exception("API DOWN"))  # Simulerer API-nedbrud
def test_sensor_data_failure(mock_get, client):  # Tester fejl-svar
    with client.session_transaction() as sess:  # Åbner session
        sess["user"] = "admin"  # Simulerer login
    res = client.get("/sensor-data")  # Kalder endpoint
    assert res.status_code == 500  # Forventer server-fejl
    assert "error" in res.json  # Tjekker fejlbesked

def test_sensor_data_requires_login(client):  # Tester at login kræves
    res = client.get("/sensor-data")  # Kalder endpoint uden login
    assert res.status_code == 401  # Forventer unauthorized
    assert "error" in res.json  # Tjekker fejlbesked
