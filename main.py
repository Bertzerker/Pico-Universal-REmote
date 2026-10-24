"""Learning remote web server for Raspberry Pi Pico 2 W (MicroPython)."""

import json
import os
import socket
import time
import network
from machine import Pin, reset

import config
import settings
from ir import IR
from tv_remote import TVRemote

DATA_FILE = "devices.json"
MAX_DEVICES = 30
MAX_BUTTONS = 64
MAX_DIMENSION = 8
ir = IR(config.IR_RX_PIN, config.IR_TX_PIN, config.IR_CARRIER_HZ)
tv_remote = TVRemote()
next_key = Pin(config.NEXT_PIN, Pin.IN, Pin.PULL_UP)
send_key = Pin(config.SEND_PIN, Pin.IN, Pin.PULL_UP)
devices = []
selected_device = 0
selected_button = 0
last_next = 0
last_send = 0
next_was_down = False
send_was_down = False
restart_pending = False
wifi_mode = ""
wifi_address = ""


def load():
    global devices
    try:
        with open(DATA_FILE) as f:
            value = json.load(f)
        devices = value if isinstance(value, list) else []
    except (OSError, ValueError):
        devices = []


def save():
    with open(DATA_FILE + ".tmp", "w") as f:
        json.dump(devices, f)
    try:
        os.remove(DATA_FILE)
    except OSError:
        pass
    os.rename(DATA_FILE + ".tmp", DATA_FILE)


def validate_device(value):
    if not isinstance(value, dict):
        raise ValueError("Invalid device")
    name = value.get("name", "")
    device_type = value.get("type", "ir")
    if not isinstance(name, str) or not name.strip() or len(name) > 40:
        raise ValueError("Device name must be 1–40 characters")
    if device_type == "bluetooth":
        if value.get("profile", "android_tv") != "android_tv":
            raise ValueError("Unsupported Bluetooth remote profile")
        return {"name": name.strip(), "type": "bluetooth", "profile": "android_tv"}
    if device_type != "ir":
        raise ValueError("Unknown remote type")
    rows, cols = value.get("rows"), value.get("cols")
    buttons = value.get("buttons")
    if type(rows) is not int or type(cols) is not int or not (1 <= rows <= MAX_DIMENSION and 1 <= cols <= MAX_DIMENSION):
        raise ValueError("Rows and columns must each be 1–8")
    if not isinstance(buttons, list) or len(buttons) != rows * cols or len(buttons) > MAX_BUTTONS:
        raise ValueError("Button count does not match the matrix")
    for i, button in enumerate(buttons):
        if not isinstance(button, dict):
            raise ValueError("Invalid button")
        label = button.get("name")
        signal = button.get("signal")
        if not isinstance(label, str) or not label.strip() or len(label) > 30:
            raise ValueError("Button names must be 1–30 characters")
        if signal is not None:
            if not isinstance(signal, list) or not (3 <= len(signal) <= IR.MAX_EDGES) or len(signal) % 2 != 1:
                raise ValueError("Invalid IR signal")
            if any(type(x) is not int or x < 80 or x > 20000 for x in signal):
                raise ValueError("Invalid IR timing")
    return {"name": name.strip(), "type": "ir", "rows": rows, "cols": cols,
            "buttons": [{"name": b["name"].strip(), "signal": b.get("signal")} for b in buttons]}


def body_json(body):
    try:
        return json.loads(body.decode())
    except (ValueError, UnicodeError):
        raise ValueError("Invalid JSON")


def find_device(index):
    if not 0 <= index < len(devices):
        raise ValueError("Device not found")
    return devices[index]


def find_button(device, index):
    if not 0 <= index < len(device["buttons"]):
        raise ValueError("Button not found")
    return device["buttons"][index]


def route(method, path, body):
    global selected_device, selected_button
    parts = path.split("?")[0].strip("/").split("/")
    if method == "GET" and parts == ["api", "devices"]:
        return 200, devices
    if method == "GET" and parts == ["api", "selection"]:
        return 200, {"device": selected_device, "button": selected_button}
    if method == "GET" and parts == ["api", "bluetooth"]:
        return 200, tv_remote.status()
    if method == "POST" and parts == ["api", "devices"]:
        if len(devices) >= MAX_DEVICES:
            raise ValueError("Device limit reached")
        devices.append(validate_device(body_json(body)))
        save()
        return 200, {"index": len(devices) - 1}
    if len(parts) >= 3 and parts[:2] == ["api", "devices"]:
        try:
            index = int(parts[2])
        except ValueError:
            raise ValueError("Invalid device index")
        device = find_device(index)
        if len(parts) == 3 and method == "PUT":
            updated = validate_device(body_json(body))
            if updated.get("type", "ir") != device.get("type", "ir"):
                raise ValueError("Create a new remote to change its type")
            devices[index] = updated
            if selected_device == index:
                selected_button = min(selected_button, max(0, len(updated.get("buttons", [])) - 1))
            save()
            return 200, {"ok": True}
        if len(parts) == 3 and method == "DELETE":
            devices.pop(index)
            selected_device = min(selected_device, max(0, len(devices) - 1))
            selected_button = 0
            save()
            return 200, {"ok": True}
        if len(parts) == 4 and parts[3] == "select" and method == "POST":
            selected_device, selected_button = index, 0
            return 200, {"ok": True}
        if len(parts) == 4 and parts[3] == "command" and method == "POST":
            if device.get("type", "ir") != "bluetooth":
                raise ValueError("Select a Bluetooth remote")
            value = body_json(body)
            if not isinstance(value, dict):
                raise ValueError("Invalid command")
            tv_remote.send(value)
            selected_device = index
            return 200, {"ok": True}
        if len(parts) == 6 and parts[3] == "buttons":
            if device.get("type", "ir") != "ir":
                raise ValueError("This remote has no learned IR buttons")
            try:
                button_index = int(parts[4])
            except ValueError:
                raise ValueError("Invalid button index")
            button = find_button(device, button_index)
            if parts[5] == "learn" and method == "POST":
                signal = ir.learn()
                button["signal"] = signal
                save()
                return 200, {"signal": signal}
            if parts[5] == "send" and method == "POST":
                if button["signal"] is None:
                    raise ValueError("Button has no learned signal")
                ir.send(button["signal"])
                selected_device, selected_button = index, button_index
                return 200, {"ok": True}
    return 404, {"error": "Not found"}


def respond(conn, status, content, mime="application/json"):
    payload = content if isinstance(content, bytes) else json.dumps(content).encode()
    conn.sendall(("HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" %
                  (status, "OK" if status == 200 else "Error", mime, len(payload))).encode())
    conn.sendall(payload)


def system_route(method, path, body, headers):
    global restart_pending
    if method == "GET" and path == "/api/system":
        return 200, {"setup": not settings.ready(), "ssid": settings.settings["ssid"] if settings.ready() else "",
                     "mode": wifi_mode, "address": wifi_address}
    if method == "POST" and path == "/api/setup" and not settings.ready():
        value = body_json(body)
        if not isinstance(value, dict):
            raise ValueError("Invalid setup")
        # Validate Wi-Fi before creating the login, so setup can be retried.
        ssid = value.get("ssid", "")
        wifi_password = value.get("wifi_password", "")
        if not isinstance(ssid, str) or len(ssid.encode()) > 32 or not isinstance(wifi_password, str) or len(wifi_password.encode()) > 63:
            raise ValueError("Invalid Wi-Fi credentials")
        if ssid and wifi_password and len(wifi_password.encode()) < 8:
            raise ValueError("Wi-Fi password must be at least 8 characters")
        token = settings.set_password(value.get("password"))
        settings.set_wifi(ssid, wifi_password)
        restart_pending = bool(ssid)
        return 200, {"token": token, "restarting": restart_pending}
    if not settings.ready():
        return 403, {"error": "Complete first-use setup"}
    if method == "POST" and path == "/api/login":
        value = body_json(body)
        if isinstance(value, dict) and settings.check_password(value.get("password")):
            return 200, {"token": settings.new_session()}
        time.sleep_ms(800)
        return 401, {"error": "Incorrect password"}
    auth = headers.get("authorization", "")
    token = auth[7:] if auth.startswith("Bearer ") else ""
    if not settings.authenticated(token):
        return 401, {"error": "Please log in"}
    if method == "POST" and path == "/api/logout":
        settings.logout(token)
        return 200, {"ok": True}
    if method == "POST" and path == "/api/password":
        value = body_json(body)
        if not isinstance(value, dict) or not settings.check_password(value.get("current")):
            return 403, {"error": "Current password is incorrect"}
        token = settings.set_password(value.get("password"))
        return 200, {"token": token}
    if method == "POST" and path == "/api/wifi":
        value = body_json(body)
        if not isinstance(value, dict):
            raise ValueError("Invalid Wi-Fi settings")
        ssid = value.get("ssid", "")
        password = value.get("password", "")
        if ssid == settings.settings["ssid"] and password == "":
            password = settings.settings["wifi_password"]
        settings.set_wifi(ssid, password)
        restart_pending = True
        return 200, {"restarting": True}
    return route(method, path, body)


def handle(conn):
    conn.settimeout(12)
    data = b""
    while b"\r\n\r\n" not in data and len(data) < 4096:
        chunk = conn.recv(1024)
        if not chunk:
            return
        data += chunk
    if b"\r\n\r\n" not in data:
        respond(conn, 400, {"error": "Headers too large"})
        return
    head, body = data.split(b"\r\n\r\n", 1)
    lines = head.decode().split("\r\n")
    method, path, _ = lines[0].split(" ", 2)
    length = 0
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            name, value = line.split(":", 1)
            headers[name.lower()] = value.strip()
    if "content-length" in headers:
        length = int(headers["content-length"])
    if length < 0:
        raise ValueError("Invalid request length")
    if length > 16384:
        respond(conn, 413, {"error": "Request too large"})
        return
    while len(body) < length:
        chunk = conn.recv(min(1024, length - len(body)))
        if not chunk:
            raise ValueError("Incomplete request")
        body += chunk
    if method == "GET" and path == "/":
        with open("index.html", "rb") as f:
            respond(conn, 200, f.read(), "text/html; charset=utf-8")
        return
    try:
        status, result = system_route(method, path, body[:length], headers)
        respond(conn, status, result)
    except ValueError as exc:
        respond(conn, 400, {"error": str(exc)})


def keys():
    global selected_button, last_next, last_send, next_was_down, send_was_down
    now = time.ticks_ms()
    next_down = next_key.value() == 0
    send_down = send_key.value() == 0
    if next_down and not next_was_down and time.ticks_diff(now, last_next) > 200:
        if devices and devices[selected_device].get("type", "ir") == "ir":
            selected_button = (selected_button + 1) % len(devices[selected_device]["buttons"])
        last_next = now
    if send_down and not send_was_down and time.ticks_diff(now, last_send) > 200:
        if devices and devices[selected_device].get("type", "ir") == "ir":
            signal = devices[selected_device]["buttons"][selected_button]["signal"]
            if signal:
                ir.send(signal)
        last_send = now
    next_was_down, send_was_down = next_down, send_down


def wifi():
    global wifi_mode, wifi_address
    if settings.ready() and settings.settings["ssid"]:
        station = network.WLAN(network.STA_IF)
        station.active(True)
        station.connect(settings.settings["ssid"], settings.settings["wifi_password"])
        start = time.ticks_ms()
        while not station.isconnected() and time.ticks_diff(time.ticks_ms(), start) < 15000:
            time.sleep_ms(200)
        if station.isconnected():
            wifi_mode = "router"
            wifi_address = station.ifconfig()[0]
            return wifi_address
        station.active(False)
    access = network.WLAN(network.AP_IF)
    access.config(essid=config.AP_SSID, password=config.AP_PASSWORD)
    access.active(True)
    wifi_mode = "access point"
    wifi_address = access.ifconfig()[0]
    return wifi_address


load()
address = wifi()
print("Learning Remote: http://%s/" % address)
server = socket.socket()
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(("0.0.0.0", 80))
server.listen(2)
server.settimeout(0.1)
while True:
    keys()
    try:
        conn, _ = server.accept()
    except OSError:
        continue
    try:
        handle(conn)
    except Exception as exc:
        try:
            respond(conn, 500, {"error": str(exc)})
        except OSError:
            pass
    finally:
        conn.close()
    if restart_pending:
        time.sleep_ms(500)
        reset()
