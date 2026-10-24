"""Persistent login and router settings for the local web interface."""

import hashlib
import json
import os
import ubinascii

FILE = "settings.json"
settings = {"salt": "", "password_hash": "", "ssid": "", "wifi_password": ""}
sessions = []


def _hex(data):
    return ubinascii.hexlify(data).decode()


def _bytes(value):
    return ubinascii.unhexlify(value)


def _hash(password, salt):
    digest = _bytes(salt) + password.encode()
    for _ in range(1000):
        digest = hashlib.sha256(digest).digest()
    return _hex(digest)


def _same(a, b):
    if len(a) != len(b):
        return False
    diff = 0
    for x, y in zip(a.encode(), b.encode()):
        diff |= x ^ y
    return diff == 0


def load():
    try:
        with open(FILE) as f:
            saved = json.load(f)
        if isinstance(saved, dict):
            for key in settings:
                if isinstance(saved.get(key), str):
                    settings[key] = saved[key]
    except (OSError, ValueError):
        pass


def save():
    with open(FILE + ".tmp", "w") as f:
        json.dump(settings, f)
    try:
        os.remove(FILE)
    except OSError:
        pass
    os.rename(FILE + ".tmp", FILE)


def ready():
    return bool(settings["salt"] and settings["password_hash"])


def _valid_password(password):
    if not isinstance(password, str) or not 10 <= len(password.encode()) <= 128:
        raise ValueError("Login password must be 10–128 bytes")


def set_password(password):
    _valid_password(password)
    settings["salt"] = _hex(os.urandom(16))
    settings["password_hash"] = _hash(password, settings["salt"])
    sessions[:] = []
    save()
    return new_session()


def check_password(password):
    return isinstance(password, str) and ready() and _same(
        _hash(password, settings["salt"]), settings["password_hash"])


def new_session():
    token = _hex(os.urandom(24))
    sessions.append(token)
    if len(sessions) > 4:
        sessions.pop(0)
    return token


def authenticated(token):
    return bool(token) and any(_same(token, candidate) for candidate in sessions)


def logout(token):
    if token in sessions:
        sessions.remove(token)


def set_wifi(ssid, password):
    if not isinstance(ssid, str) or len(ssid.encode()) > 32:
        raise ValueError("Wi-Fi name must be at most 32 bytes")
    if not isinstance(password, str) or len(password.encode()) > 63:
        raise ValueError("Wi-Fi password must be at most 63 bytes")
    if ssid and password and len(password.encode()) < 8:
        raise ValueError("Wi-Fi password must be at least 8 characters")
    settings["ssid"] = ssid
    settings["wifi_password"] = password
    save()


load()
