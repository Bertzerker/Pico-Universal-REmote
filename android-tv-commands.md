# Android TV command research — Xiaomi TV S Mini LED 2025

Research date: 2026-10-23. The TV has paired with the Pico 2 W as a BLE keyboard and, separately, as a BLE five-button mouse. The Pico currently runs the temporary mouse test firmware. Only the observations labelled **tested** below are confirmed on this TV.

## Three different command paths

1. **BLE HID** is the path we have actually paired with the TV. It sends keyboard, mouse, and consumer-control HID *usages*. Android translates many standard usages into Android `KeyEvent` codes. The TV can still ignore a code in a particular screen or app.
2. **Google TV network remote service** sends Android keycodes and other messages over the local network after its own pairing. This is what [androidtvremote](https://github.com/harimoradiya/androidtvremote) implements; its source connects to ports 6467 for pairing and 6466 for remote control. Its app links and microphone streaming are features of this network protocol, not BLE keyboard commands. We have not checked whether this Xiaomi TV exposes that service on the user's network.
3. **IR** is what [DIYRemote](https://github.com/shannah/DIYRemote) implements: an HTML interface in an Android phone app calls `transmit()` on the phone's IR emitter. It offers UI ideas but no Bluetooth or Android TV BLE command map for this Bluetooth-only TV.

The [Xiaomi ESPHome BLE remote](https://github.com/shammysha/esphome-ble-mi-remote) is the closest command example. Its source declares a BLE consumer-control report containing Menu Pick, directional menu controls, Power, Voice, Volume, AC Home, AC Back, and other usages, plus a keyboard report. It targets **ESP32**, so its code cannot simply be copied onto the Pico. Its `SPECIAL_BACK = 19` and similar numbers are positions in its 24-bit report, **not** Android keycodes or HID usage numbers. See its [report descriptor](https://github.com/shammysha/esphome-ble-mi-remote/blob/main/components/ble_mi_remote/ble_mi_remote.cpp) and [button definitions](https://github.com/shammysha/esphome-ble-mi-remote/blob/main/components/ble_mi_remote/const.py).

## Command candidates for the Pico BLE remote

`07:xxxx` means HID Keyboard/Keypad page 0x07. `0C:xxxx` means HID Consumer page 0x0C. A matching Android keycode is useful evidence, but it does not prove the Xiaomi TV accepts that HID usage.

| Remote action | BLE HID candidate | Android result / status |
| --- | --- | --- |
| D-pad | `07:004F` right, `07:0050` left, `07:0051` down, `07:0052` up; or consumer menu directions | Standard Android mapping; **not yet tested with Pico**. |
| Select | `07:0028` Enter; possibly consumer Menu Pick `0C:0041` | Test both on home and in apps. |
| Back / Exit | **Mouse button 4**; alternatively consumer AC Back `0C:0224`, keyboard Back `07:00F1`, or Escape `07:0029` | **Mouse button 4 tested working.** Escape also caused Back on one screen. Android documents Escape fallback to Back. Backspace is a different key (`07:002A`) and had no visible effect outside text input. |
| Home | Consumer AC Home `0C:0223` | Maps to Android `KEYCODE_HOME`. The ordinary keyboard Home key `07:004A` generally maps to cursor movement, so do not use it as the first candidate. |
| Menu | Consumer Menu `0C:0040` | Maps to Android `KEYCODE_MENU`; test whether it opens the expected TV menu. |
| Volume up/down/mute | Consumer `0C:00E9` / `0C:00EA` / `0C:00E2` | Standard Android mappings. Test actual TV audio, including while HDMI input is active. |
| Play/pause | Consumer `0C:00CD` | Standard Android mapping; test per streaming app. |
| Guide / EPG | Consumer Media Select Program Guide `0C:008D` | Maps to Android `KEYCODE_GUIDE`; likely relevant only in live TV. |
| Channel up/down | Consumer `0C:009C` / `0C:009D` | Standard Android mappings; test in live TV. |
| Captions | Consumer `0C:0061` | Maps to Android `KEYCODE_CAPTIONS`; test in live TV and apps. |
| Colored buttons | Consumer `0C:0069` red, `006A` green, `006B` blue, `006C` yellow | Android has matching programmable color keycodes. |
| Power / wake | Consumer Power `0C:0030` or another system-control usage | The Xiaomi ESPHome project advertises a Power control and has separate wake logic, but this TV's behavior remains **unverified**. Do not assume a sleeping TV keeps the BLE link. |
| Source / HDMI switching | No verified standard HID mapping for Android `KEYCODE_TV_INPUT` | Android defines the keycode, including HDMI-specific codes, but generating those over generic BLE HID needs device-specific proof. Candidate for a separate network-remote test. |
| Settings | No verified BLE HID mapping for Android `KEYCODE_SETTINGS` | Android defines the keycode; test a menu/navigation path if BLE cannot generate it directly. |
| Teletext / TTX | No verified BLE HID mapping for Android `KEYCODE_TV_TELETEXT` | Android defines the keycode; availability also depends on broadcast/app support. |
| Voice / Google Assistant | Xiaomi ESPHome report includes consumer Voice Command `0C:00CF` | A button event alone does not provide microphone audio. Test whether it opens voice UI; actual voice input needs an audio path or the network remote protocol. |
| YouTube, Netflix, Prime Video | No universal BLE HID app key | The network app uses app links; a BLE-only remote may need tested navigation sequences or TV-specific usages. Avoid assuming fixed app shortcuts. |
| Text input | BLE keyboard character reports, Backspace `07:002A`, Enter `07:0028` | Test with a TV text field focused, including punctuation and the user's keyboard layout. |

The authoritative starting point for the HID-to-Android mappings is the [Android keyboard device table](https://source.android.com/docs/core/interaction/input/keyboard-devices). Android's [mouse button documentation](https://source.android.com/docs/core/interaction/input/touch-devices) explains that a Back/Side mouse button can synthesize `KEYCODE_BACK`, matching our button-4 observation. Android's [KeyEvent reference](https://developer.android.com/reference/android/view/KeyEvent) defines TV-specific keys such as Input, Settings, Guide, Teletext, and Voice Assist, but **Android keycode numbers are not BLE HID usage numbers**.

## What the network app demonstrates

The [androidtvremote UI](https://github.com/harimoradiya/androidtvremote/blob/main/app/src/main/java/com/hari/androidtvremote/ui/app/RemoteScreen.kt) sends D-pad, Center, Back, Home, Menu, volume/mute, channel, playback, and power keycodes. Its [message builder](https://github.com/harimoradiya/androidtvremote/blob/main/app/src/main/java/com/hari/androidtvremote/androidLib/remote/RemoteMessageManager.java) sends a `RemoteKeyInject` with keycode and press direction. Its [view model](https://github.com/harimoradiya/androidtvremote/blob/main/app/src/main/java/com/hari/androidtvremote/ui/app/TvRemoteViewModel.kt) also uses IME text messages, app-link launch requests for YouTube/Netflix/Prime Video, and a separate voice session. These are useful candidates for a *future* network test if the TV exposes that service; they cannot be fed directly into a BLE HID report.

## Next experiment

Build one Pico BLE HID service with keyboard, five-button mouse, and consumer-control reports. Preserve the confirmed mouse-button-4 Back action. Test D-pad/Enter, AC Home, volume/mute, Guide, channels, captions, and power in that order. Log each command, TV screen, and result. Check reconnection after reboot before building the web UI around those commands.
