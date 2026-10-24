# Pico Universal REmote

A Raspberry Pi Pico 2 W universal remote with a phone web interface, IR learning
and playback, and Bluetooth HID control for Android TV devices.

The current firmware is in `combined_test/`. It runs Wi-Fi and Bluetooth at the
same time, offers separate Xiaomi TV and Fire TV Stick remotes, and lets you
create and edit IR remotes through the router-connected page. Bluetooth bonds
and learned IR codes survive a restart. The IR editor shows the last received
code and supports button names, colors, and grid sizing. A touchpad, mouse
click, navigation keys, media controls, and text input are available for the
Bluetooth remotes.

## Hardware

- Raspberry Pi Pico 2 W
- IR receiver on GP16, with level protection described in `schematic.md`
- 38 kHz IR transmitter on GP17

See `hardware.md` and the schematic PDF for the board layout.

## Source layout

- `combined_test/`: current Pico SDK firmware and web page
- `bluetooth_test/`: shared Bluetooth HID implementation and experiments
- Root Python and HTML files: earlier MicroPython version
- `todo.md` and `pico remote.md`: project progress and planned features

## Building the current firmware

The Pico SDK and Pico examples source tree are required. The CMake project
uses the examples tree for its access point DHCP and DNS helpers. Before
building, run the three generators in `combined_test/`:

1. `generate_settings.py` reads private Wi-Fi and login settings from
   `pico_backup/settings.json` and `pico_backup/config.py`.
2. `generate_ir.py` imports the original learned IR signals from
   `pico_backup/devices.json`.
3. `generate_web.py` embeds the current web page.

These generated headers go into `combined_test/build/`. The backup, generated
headers, downloaded toolchains, and build outputs are excluded from Git because
the backup contains private network credentials. Configure CMake with
`PICO_SDK_PATH` set to your Pico SDK checkout, then build `combined_test/`.

The current firmware was tested on a Xiaomi TV S Mini LED (2025), a Fire TV
Stick, and an IR HDMI switch. See `todo.md` for remaining work and test notes.
