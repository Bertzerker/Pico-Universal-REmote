# Universal Remote — brainstorming backlog

## Goal

Keep the Pico 2 W Wi-Fi web app as the phone interface, and add Bluetooth control for the smart TV and streaming devices. The smart TV in this project is **Bluetooth-only**: do not plan an IR fallback for its controls. The existing IR learning remote remains useful for other devices.

The first TV target is a **Xiaomi TV S Mini LED (2025)** running Google TV. The Pico should appear to supported targets as a Bluetooth keyboard/media remote (and possibly a mouse). Exact commands must be verified on each target; a keyboard working for navigation does not guarantee every button works.

## Existing foundation

- [x] Wi-Fi web interface with login and router setup.
- [x] Device selection and configurable button grid.
- [x] IR learning and playback for devices that use IR.
- [ ] Resolve the physical-button plan: the earlier note says button 1 enters bootloader and button 2 is unused; current firmware uses next/send.

## Next version progress (October 24, 2026)

- [x] The combined Pico SDK firmware serves a home page that selects the Bluetooth Android TV or the backed-up HDMI Switch Matrix IR remote.
- [x] The original IR button timings and labels are embedded from `pico_backup/devices.json`; the user confirmed a known HDMI Switch Matrix button responds.
- [x] Keep up to six separate web login sessions so opening the phone page does not log out the PC browser.
- [x] Add a paced touchpad movement queue and set the BLE mouse movement before asking BTstack to send it. The user confirmed the TV pointer moves from the phone touchpad.
- [x] Add a phone touchpad speed slider (1×–8×, default 3×) and remember its value per browser.
- [x] Add navigation pages for remote selection, editing, and Pico settings. The settings page still needs editable configuration controls.
- [x] Restore IR create, rename, grid resize, button label/color, delete, and learn operations through the router page; save the IR database in Pico flash. The user created a temporary IR remote and confirmed that its learned state survived reboot.
- [x] Repair and verify learned IR playback. The original failed capture stored only an NEC repeat frame (`9060, 2208, 627`). Capture now rejects short repeats and incomplete bursts. USB logs showed full 67-timing frames. Saving initially failed because the shared 4 KiB flash bank could not append another full IR database; version 2 of the saved format packs usual pulse widths into one byte while retaining support for old version 1 data. The Pico logged successful saves, and the user confirmed the new remote appears and its learned button works. The router page also returned after the final firmware update; Wi-Fi startup now retries before falling back to its access point.
- [x] Save and switch between Xiaomi TV and Fire TV Stick Bluetooth pairings. Both targets responded to Right Arrow after switching, and Fire reconnected and worked after a Pico reboot. The firmware caps queued touchpad movement and checks the target on every command.
- [ ] Add a forget-device control for individual Bluetooth bonds.
- [ ] Make Pico login and Wi-Fi settings editable in the combined firmware.
- [x] Verify the larger page, IR sending, and touchpad from the phone after flashing. The user confirmed both remotes appear and both controls work.

## First experiments — compatibility before implementation

- [x] Identify the first TV target: Xiaomi TV S Mini LED (2025). Record its screen size and firmware version when available.
- [ ] Record the Fire TV Stick model and test it separately.
- [ ] Pair a normal Bluetooth keyboard and mouse with each target. Make a table of which keys work in the home screen, settings, live TV, and streaming apps.
- [ ] Determine whether each target accepts **BLE HID**, **Bluetooth Classic HID**, or both. The current MicroPython Bluetooth API is BLE-oriented; some Fire TV models lack BLE support.
- [x] Build a Pico SDK / BTstack BLE HID keyboard prototype and pair it with the Xiaomi TV. Paired using the official `hog_keyboard_demo` on Pico 2 W; Escape sent over USB serial made the TV go Back. A separate Backspace press had no visible effect outside a text field. Test Backspace again with a text field focused; bonding and reconnect after restart still need verification.
- [ ] Integrate the proven Bluetooth stack with the Wi-Fi and IR app while preserving saved settings and learned codes.
- [ ] Check whether Bluetooth Classic HID is needed for the later Fire TV target.
- [ ] Keep a measured compatibility matrix: action, HID usage/key, target model, screen/app tested, result, and notes.

## Commands to investigate

| Web remote action | Bluetooth candidate | Question to test |
| --- | --- | --- |
| Up/down/left/right; select | Keyboard arrows and Enter | Does focus navigation work everywhere? |
| Back / exit menu | Mouse button 4 (confirmed); Escape also caused Back in one test | Prefer button 4, then verify across home, settings, live TV, and apps. |
| Home; Settings; menu | HID Home/Menu or target-specific key | Does the OS reserve or remap these? |
| Play/pause; rewind; fast-forward | HID media controls | Which apps honor them? |
| Mute; volume up/down | HID consumer controls | Does the TV volume change, or only the streaming device? |
| Channel up/down | HID channel controls | Does live TV respond? |
| EPG / program guide; teletext | Target-specific HID usage/key | Are these functions available at all on this model? |
| Power on/off; source switching | System/consumer HID controls if supported | Can Bluetooth wake the TV, and does it expose an input selector? |
| Apps list; YouTube; Prime Video; Netflix | Target-specific shortcuts or navigation macros | There is no universal keyboard key for each app. |
| Google Voice / Alexa voice | Separate research task | A key event alone may not provide microphone audio or invoke voice control. |
| Search / keyboard input | HID keyboard reports | Layout, punctuation, focus, Unicode, and per-app support |
| Pointer / mouse | HID mouse reports | Does the target accept pointer input? |

Treat every row as a hypothesis until confirmed on the actual target. Do not silently map an unsupported action to another command.

## Firmware backlog

- [ ] Define an action model: selected device + action + optional text/hold duration → target-specific Bluetooth HID report or other supported transport.
- [x] Prototype BLE HID peripheral with keyboard, consumer-control, and five-button mouse reports in one Pico SDK test image. Pairing and core navigation work on the Xiaomi TV; integration with the web app remains.
- [ ] Send distinct key-down/key-up reports. Support long press and repeat for navigation, volume, and channels.
- [ ] Add pairing mode, bonded-device storage, reconnect, forget-device control, and connection-status reporting.
- [x] Prove Wi-Fi station and paired BLE HID operate together in a Pico SDK image. The combined test connected to the router at `192.168.8.126`, served `/health` and the TV page, and kept all HID reports subscribed after reboot with persistent bond storage.
- [ ] Test web responsiveness, Bluetooth latency, and IR capture under simultaneous traffic in the final integrated image.
- [ ] Keep Bluetooth mappings separate from learned IR data; migrate existing device profiles without losing saved signals.
- [ ] Make the active target explicit so commands cannot accidentally go to another paired device.

## Smart TV web app

- [ ] Create a phone-friendly remote view: power/source, D-pad/select, Back/Home/Menu, volume/mute, channel/guide, playback, and app shortcuts.
- [ ] Let users hide unsupported buttons and rearrange or rename supported ones per target.
- [ ] Add text input with Send, Backspace, Enter, and basic keyboard shortcuts. Show when the TV must have a text field focused. Test Unicode and non-US layouts separately.
- [ ] Add optional touchpad view if the TV accepts a Bluetooth mouse.
- [ ] Show active TV/stick, Bluetooth connection state, last command, and a useful error for an unmapped action.
- [ ] Keep the existing IR grid accessible for other devices.

## First milestone

- [x] Pair the Pico as a Bluetooth keyboard with **one specific TV** (Xiaomi TV S Mini LED 2025) using temporary SDK test firmware.
- [ ] Pair again with Bluetooth HID integrated into the Wi-Fi web/IR firmware, so both work at once.
- [ ] From the web page, navigate with arrows/Enter, use Back/Home where the TV supports them, control media and volume where supported, and type into a focused search field.
- [ ] Document every tested button and clearly list unsupported commands, especially power, source, voice, and app launch.
- [ ] Then test a named Fire TV Stick model as a separate profile.

## Xiaomi TV S Mini LED 2025 input results

The Pico 2 W paired as the SDK BLE keyboard and then as a separate BLE mouse. The TV responded to Escape from the keyboard. With the mouse test firmware, button 2 (right-click) and button 3 (middle-click) had no visible effect; **button 4 went Back/Exit**; button 5 had no visible effect. These were single-screen tests. The active Pico firmware is now the combined Wi-Fi/Bluetooth image in `combined_test/`, with backed-up IR buttons embedded for playback. The working MicroPython files and matching firmware image are saved in `pico_backup/`.

The first combined-device pairing showed Connected, but the Pico saw no notification subscriptions for any of its three input reports or the boot reports. The TV retained an older GATT profile because the earlier test devices shared the Pico's Bluetooth identity. After removing “Pico TV Test,” “HID Keyboard,” and “HID Mouse” from the TV and pairing “Pico TV Test” again, the TV subscribed to all three reports and both boot reports. The Pico's `?` diagnostic returned `reports=1,1,1 boot=1,1`.

Confirmed on the newly paired combined device: mouse button 4 = Back, consumer AC Home = Home, keyboard Right Arrow moves focus, keyboard Enter selects the highlighted item, consumer Volume Up raises TV volume, consumer Mute toggles mute, relative mouse movement moves the pointer, and mouse button 1 clicks. The Pico reported successful report sends and the user observed each TV response. Mute was sent again afterward to restore audio. Text input is confirmed: the test firmware typed `Pico2` into a focused TV search field. Other commands in the test firmware are candidates only until tested on the TV.

The MicroPython web app source now supports adding an IR or Bluetooth remote and includes an Android TV layout with text input, touchpad, and Mouse 1. Its Bluetooth endpoint deliberately reports unavailable because the deployed firmware uses Pico SDK/BTstack instead. The active `combined_test/` image has Wi-Fi + BLE HID coexistence, persistent TV bonding, a login-protected home page, Android TV controls, and the backed-up IR buttons available to send. The original IR learning and device editor are **not yet ported** into that SDK image. Keep the backed-up MicroPython files and learned IR signals intact. Next: verify IR and touchpad on hardware, then implement persistent editing, setup controls, and multiple Bluetooth target bonds.

See [android-tv-commands.md](android-tv-commands.md) for the researched BLE HID usage candidates, Android keycode distinction, and evaluation of the three linked projects. Priority commands for the combined HID test: mouse button 4 for Back, keyboard arrows/Enter, consumer AC Home, volume/mute, media, Guide, and channels. Source switching, Settings, Teletext, voice audio, and app shortcuts have no confirmed generic BLE mapping on this TV.

## Current pairing blocker

The deployed RP2 MicroPython firmware can advertise BLE and create GATT services, but a direct probe of its `bluetooth.BLE` object found no `gap_pair()` method and no `bond`, `le_secure`, or `io` configuration parameters. These are the pairing/bonding controls needed for a dependable Bluetooth HID keyboard connection. The Xiaomi TV cannot be paired with the current firmware just by opening its pairing screen. The Pico SDK includes BTstack BLE and Bluetooth Classic support, so a small SDK HID prototype is the next practical pairing test. Flashing an SDK test image would temporarily replace the running MicroPython web/IR firmware; back up the Pico files and restore it after the test before integrating the two functions.

## Research references

- [MicroPython Bluetooth API](https://docs.micropython.org/en/latest/library/bluetooth.html): BLE peripheral and GATT building blocks.
- [Xiaomi TV S Mini LED 2025 specifications](https://www.mi.com/global/product/xiaomi-tv-s-mini-led-55-2025/specs/): Google TV and Bluetooth 5.2 (55-inch model; confirm screen size).
- [Pico SDK Bluetooth networking libraries](https://www.raspberrypi.com/documentation/pico-sdk/networking.html): BTstack BLE and Classic support.
- [Android HID key mapping](https://source.android.com/docs/core/interaction/input/keyboard-devices): keyboard and consumer-control usages, with device-specific variation.
- [Android mouse button mapping](https://source.android.com/docs/core/interaction/input/touch-devices): Back/Side button synthesizes Android Back.
- [Xiaomi ESPHome BLE remote](https://github.com/shammysha/esphome-ble-mi-remote): ESP32 consumer-control and keyboard HID report example.
- [DIYRemote](https://github.com/shannah/DIYRemote): HTML-designed IR remote for Android phones; UI reference only for this Bluetooth TV.
- [androidtvremote](https://github.com/harimoradiya/androidtvremote): separate Google TV network remote protocol with keycodes, IME, app links, and voice.
- [Amazon Fire TV remote input](https://developer.amazon.com/docs/fire-tv/ja-remote-input.html): Fire TV navigation/media behavior and special buttons.
- [Amazon Fire TV device specifications](https://developer.amazon.com/docs/fire-tv/device-specifications-fire-tv-streaming-media-player.html): Bluetooth and BLE support varies by model.
