"""Generate a local, untracked C header from the backed-up Pico settings.

Run before configuring the combined test firmware. Do not print credentials.
"""

import json
import ast
from pathlib import Path

root = Path(__file__).resolve().parents[1]
settings = json.loads((root / "pico_backup" / "settings.json").read_text())
config_tree = ast.parse((root / "pico_backup" / "config.py").read_text())
config = {}
for node in config_tree.body:
    if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
        if node.targets[0].id in ("AP_SSID", "AP_PASSWORD"):
            config[node.targets[0].id] = ast.literal_eval(node.value)
ssid = settings.get("ssid", "")
password = settings.get("wifi_password", "")
salt = settings.get("salt", "")
password_hash = settings.get("password_hash", "")
if not ssid or not password or len(salt) != 32 or len(password_hash) != 64 or not config.get("AP_SSID") or not config.get("AP_PASSWORD"):
    raise SystemExit("Backed-up router settings are incomplete")
out = Path(__file__).resolve().parent / "build" / "local_settings.h"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("#define LOCAL_WIFI_SSID " + json.dumps(ssid) + "\n"
               "#define LOCAL_WIFI_PASSWORD " + json.dumps(password) + "\n"
               "#define LOCAL_PASSWORD_SALT " + json.dumps(salt) + "\n"
               "#define LOCAL_PASSWORD_HASH " + json.dumps(password_hash) + "\n"
               "#define LOCAL_AP_SSID " + json.dumps(config["AP_SSID"]) + "\n"
               "#define LOCAL_AP_PASSWORD " + json.dumps(config["AP_PASSWORD"]) + "\n")
print("Generated local Wi-Fi settings header")
