# Pico Universal REmote

The current Pico 2 W firmware lives in `combined_test/`. It serves a phone web
remote over Wi-Fi, sends and learns IR commands, and controls paired Android TV
devices over Bluetooth HID. The Xiaomi TV and Fire TV Stick targets can be
selected from the web page. Learned IR remotes and Bluetooth bonds persist in
flash. The receiver is on GP16 and the IR transmitter is on GP17.

The working hardware state and remaining ideas are recorded in `todo.md` and
`pico remote.md`. `bluetooth_test/` contains the shared Bluetooth HID source.
The older MicroPython version is retained in the root files below.

Private router settings and Pico backups stay on the local computer and are
excluded from Git. The SDK build currently needs `pico_backup/settings.json`,
`pico_backup/config.py`, and `pico_backup/devices.json` to generate local headers
with `combined_test/generate_settings.py` and `combined_test/generate_ir.py`.
It also needs the Pico SDK and Pico examples tree for the access point helpers.

## Bluetooth TV work in progress

The source web app now offers **Add remote → IR remote / Bluetooth remote**. The Bluetooth view has Android TV navigation, volume, text entry, and a touchpad with Mouse 1 click. The currently deployed Pico SDK test firmware has paired with a Xiaomi TV S Mini LED (2025) and confirmed Back, Home, Right Arrow, Select, Volume Up, Mute, pointer movement, left click, and typing `Pico2` into a focused TV search field.

The `combined_test/` Pico SDK image now runs Wi-Fi and the paired Bluetooth HID together, keeps the TV bond across reboots, and serves a login-protected Bluetooth TV page on the Pico's router address. It uses the backed-up router and web-login settings. The access point appears only if the router connection fails. IR learning, stored IR devices, and the full IR/Bluetooth device selector still need to be ported into this image. Keep the backup in `pico_backup/`; the original MicroPython web/IR image remains available for restoration. See `todo.md` for the integration work.

MicroPython firmware for a Wi-Fi IR learning remote. The web page lets you add devices, set a row × column button matrix, rename buttons, learn IR signals, send learned signals, edit devices, and delete devices. Two physical buttons select the next button and transmit the selected button. Choose a device on the web page to set the physical buttons' active device.

## Setup

1. Flash a current **Raspberry Pi Pico 2 W MicroPython** UF2 from the official MicroPython download page.
2. Follow [schematic.md](schematic.md), including the receiver level protection. Keep the Pico connected to USB power.
3. Change `AP_PASSWORD` in `config.py` to a private password of 8–63 characters before the first boot. The access point is used for first-time setup and as a fallback if the router cannot be reached.
4. Copy `config.py`, `settings.py`, `ir.py`, `main.py`, and `index.html` to the Pico filesystem with Thonny or `mpremote`. Reset the board.
5. Join the `Learning-Remote` Wi-Fi network using your configured access point password. Read the Pico serial console for the displayed `http://.../` address and open it.
6. On the first visit, create a web login password of at least 10 characters. Optionally enter your router's Wi-Fi name and password. The Pico saves these details in `settings.json` and restarts. Connect your phone/computer to your router and open the Pico's new address, shown in the serial console or your router's connected-device list. Log in with the web password you created.

In **Settings**, you can later change the web login password or router credentials. Changing Wi-Fi restarts the Pico. If router connection fails at startup, the access point comes back so you can correct the credentials. The web login password is stored as a salted hash; the router password must be stored on the Pico to reconnect automatically. Sessions end when the Pico restarts or when you log out.

## Use

Click **Add device**, enter its name and matrix dimensions, rename buttons if desired, then **Save**. Choose **Edit device** and click **Learn** next to a button. Aim the original remote at the receiver and press the corresponding button once. The page displays the received mark/space timings. Repeat for other buttons. On the main matrix, click a learned button to transmit it. The two hardware buttons step through the active device's buttons and send the selected one; selection is also shown by `/api/selection` for integrations.

## Limits

- Supports up to 30 devices, an 8 × 8 matrix per device, and 600 captured edges per signal. Signals are stored in `devices.json` on the Pico.
- Learning captures one IR frame. Some remotes need repeats, toggle bits, or unusual carrier frequencies and may not replay reliably. The carrier is configured in `config.py` (default 38 kHz).
- Software timing and Wi-Fi activity can introduce jitter. For protocols with tight tolerances, a PIO-based transmitter and receiver would be more reliable.
- The local HTTP page has a password login but does not use TLS. Use it only on a trusted Wi-Fi network or the password-protected device access point; do not forward port 80 from the internet. The setup screen is available only until a login password is created.
