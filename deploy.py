"""Copy this project's MicroPython files to a Pico 2 W over USB serial.

Requires pyserial: python -m pip install pyserial
Usage: python deploy.py COM3 [file.py ...]
"""

import base64
import hashlib
import pathlib
import sys
import time

import serial


FILES = ("config.py", "settings.py", "ir.py", "tv_remote.py", "index.html", "main.py")
ROOT = pathlib.Path(__file__).resolve().parent


def until(port, marker, seconds=10):
    deadline = time.monotonic() + seconds
    data = bytearray()
    while marker not in data:
        if time.monotonic() > deadline:
            raise TimeoutError("Pico did not respond: " + repr(bytes(data[-160:])))
        data.extend(port.read(1))
    return bytes(data)


def command(port, code):
    port.write(code.encode() + b"\x04")
    reply = until(port, b"OK", 10)
    if not reply.endswith(b"OK"):
        raise RuntimeError("Unexpected raw REPL response: " + repr(reply))
    output = until(port, b"\x04", 15)[:-1]
    error = until(port, b"\x04", 15)[:-1]
    until(port, b">", 5)
    if error:
        raise RuntimeError(error.decode(errors="replace"))
    return output.decode(errors="replace").strip()


def upload(port, name):
    data = (ROOT / name).read_bytes()
    stage = name + ".upload"
    command(port, "import ubinascii; f=open(%r,'wb')" % stage)
    for offset in range(0, len(data), 192):
        encoded = base64.b64encode(data[offset:offset + 192]).decode()
        command(port, "f.write(ubinascii.a2b_base64('%s'))" % encoded)
    command(port, "f.close()")
    remote_hash = command(port, "import hashlib,ubinascii; print(ubinascii.hexlify(hashlib.sha256(open(%r,'rb').read()).digest()).decode())" % stage)
    local_hash = hashlib.sha256(data).hexdigest()
    if remote_hash != local_hash:
        raise RuntimeError("Verification failed for " + name)
    print("Staged and verified:", name, len(data), "bytes")


def install(port, name, existing):
    stage = name + ".upload"
    backup = name + ".backup"
    if backup in existing:
        command(port, "import os; os.remove(%r)" % backup)
    if name in existing:
        command(port, "import os; os.rename(%r,%r)" % (name, backup))
    command(port, "import os; os.rename(%r,%r)" % (stage, name))
    print("Installed:", name)


def main():
    if len(sys.argv) < 2:
        raise SystemExit("Usage: python deploy.py COM3 [file.py ...]")
    files = tuple(sys.argv[2:]) or FILES
    if any(name not in FILES for name in files):
        raise SystemExit("Only project files may be deployed")
    with serial.Serial(sys.argv[1], 115200, timeout=0.1, write_timeout=5) as port:
        port.write(b"\x03\x03")
        time.sleep(0.2)
        port.reset_input_buffer()
        port.write(b"\x01")
        greeting = until(port, b">", 5)
        if b"raw REPL" not in greeting:
            raise RuntimeError("COM port is not a MicroPython REPL: " + repr(greeting))
        print(command(port, "import sys; print(sys.implementation)"))
        existing = command(port, "import os; print(','.join(os.listdir()))").split(",")
        print("Existing files:", ", ".join(existing))
        for name in files:
            upload(port, name)
        for name in files:
            install(port, name, existing)
        print("Final files:", command(port, "print(','.join(os.listdir()))"))
        port.write(b"\x02")  # Friendly REPL, no reset; main.py starts on next boot.
    print("Transfer complete. Reset the Pico to run main.py.")


if __name__ == "__main__":
    main()
