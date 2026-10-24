"""Back up the Pico MicroPython filesystem over its USB serial port.

Usage: python backup_pico.py COM3
The backup can contain Wi-Fi credentials and the web login password.
"""

import base64
from pathlib import Path
import sys
import time

import serial

from deploy import command, until


def main():
    port_name = sys.argv[1]
    destination = Path(__file__).resolve().parent / "pico_backup"
    destination.mkdir(exist_ok=True)
    with serial.Serial(port_name, 115200, timeout=0.1, write_timeout=5) as port:
        port.write(b"\x03\x03")
        time.sleep(0.2)
        port.reset_input_buffer()
        port.write(b"\x01")
        greeting = until(port, b">", 5)
        if b"raw REPL" not in greeting:
            raise RuntimeError("Pico did not enter raw REPL")
        names = command(port, "import os; print(','.join(os.listdir()))").split(",")
        for name in names:
            if not name or "/" in name or "\\" in name or name in (".", ".."):
                continue
            is_dir = command(port, "import os; print(os.stat(%r)[0] & 0x4000 != 0)" % name)
            if is_dir == "True":
                continue
            data = command(port, "import ubinascii; print(ubinascii.b2a_base64(open(%r,'rb').read()).decode())" % name)
            content = base64.b64decode(data)
            (destination / name).write_bytes(content)
            print("Saved", name, len(content), "bytes")
        port.write(b"\x02\x04")
    print("Backup saved to", destination)


if __name__ == "__main__":
    main()
