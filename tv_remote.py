"""Android TV command contract for the future combined Wi-Fi/BLE firmware.

The stock MicroPython image cannot use the paired Pico SDK Bluetooth HID stack.
Keep commands explicit here so the web UI never claims an untested key works.
"""


KEYS = ("up", "down", "left", "right", "select", "back", "home",
        "volume_up", "volume_down", "mute", "play_pause", "mouse_click")


class TVRemote:
    def status(self):
        return {"available": False, "connected": False,
                "message": "Bluetooth TV control needs combined Pico firmware"}

    def send(self, command):
        action = command.get("action")
        if action == "key":
            if command.get("key") not in KEYS:
                raise ValueError("Unknown TV button")
        elif action == "text":
            value = command.get("text")
            if not isinstance(value, str) or not 1 <= len(value) <= 80:
                raise ValueError("Enter 1–80 characters")
            if any(ord(c) < 32 or ord(c) > 126 for c in value):
                raise ValueError("Only basic keyboard characters are supported for now")
        elif action == "mouse_move":
            dx, dy = command.get("dx"), command.get("dy")
            if type(dx) is not int or type(dy) is not int or not (-127 <= dx <= 127 and -127 <= dy <= 127):
                raise ValueError("Invalid touchpad movement")
        else:
            raise ValueError("Unknown TV command")
        raise ValueError("Bluetooth TV control needs combined Pico firmware")
