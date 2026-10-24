"""Embed the backed-up IR remotes in the combined Pico firmware."""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
devices = json.loads((root / "pico_backup" / "devices.json").read_text())
out = Path(__file__).resolve().parent / "build" / "learned_ir.h"
out.parent.mkdir(parents=True, exist_ok=True)
lines = ["#pragma once", "#include <stdint.h>",
         "typedef struct { const char *name; const uint16_t *signal; uint16_t count; } ir_button_t;",
         "typedef struct { const char *name; const ir_button_t *buttons; uint16_t count; uint8_t cols; } ir_remote_t;"]
for di, device in enumerate(devices):
    for bi, button in enumerate(device["buttons"]):
        signal = button.get("signal")
        if signal:
            if len(signal) > 600 or len(signal) < 3 or len(signal) % 2 != 1 or any(not 80 <= x <= 20000 for x in signal):
                raise SystemExit(f"Invalid IR signal at remote {di}, button {bi}")
            lines.append(f"static const uint16_t ir_signal_{di}_{bi}[] = {{" + ",".join(map(str, signal)) + "};")
    lines.append(f"static const ir_button_t ir_buttons_{di}[] = {{")
    for bi, button in enumerate(device["buttons"]):
        signal = button.get("signal")
        ptr = f"ir_signal_{di}_{bi}" if signal else "NULL"
        lines.append("{" + json.dumps(button["name"], ensure_ascii=True) + f",{ptr},{len(signal) if signal else 0}" + "},")
    lines.append("};")
lines.append("static const ir_remote_t ir_remotes[] = {")
for di, device in enumerate(devices):
    lines.append("{" + json.dumps(device["name"], ensure_ascii=True) +
                 f",ir_buttons_{di},{len(device['buttons'])},{device.get('cols', 3)}" + "},")
lines.append("};")
lines.append(f"static const unsigned ir_remote_count = {len(devices)};")
catalog = [{"name": d["name"], "cols": d.get("cols", 3),
            "buttons": [{"name": b["name"], "learned": bool(b.get("signal"))}
                        for b in d["buttons"]]} for d in devices]
lines.append("static const char ir_catalog_json[] = " +
             json.dumps(json.dumps(catalog, ensure_ascii=True, separators=(",", ":"))) + ";")
out.write_text("\n".join(lines) + "\n")
print(f"Embedded {len(devices)} IR remotes")
