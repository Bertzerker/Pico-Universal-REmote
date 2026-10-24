# Learning Remote wiring schematic

```text
                 Raspberry Pi Pico 2 W
               ┌─────────────────────────┐
 USB 5 V ──────┤ VBUS / pin 40           │
 Ground ───────┤ GND / pin 38            │
               │                         │
 Receiver DAT ─┤ GP16 / pin 21           │
 Emitter DAT ──┤ GP17 / pin 22           │
 Next button ──┤ GP14 / pin 19           │
 Send button ──┤ GP15 / pin 20           │
               └─────────────────────────┘

  5 V IR emitter module         5 V IR receiver module
  VCC ── VBUS                   VCC ── VBUS
  GND ── GND                    GND ── GND
  DAT ── GP17                   DAT ── [10 kΩ] ── GP16
                                              │
                                            [18 kΩ]
                                              │
                                             GND

  Next button: GP14 ── switch ── GND
  Send button: GP15 ── switch ── GND
```

Use a common ground for the Pico and both IR modules. Power the 5 V modules from **VBUS only while the Pico is powered by USB**. The 10 kΩ/18 kΩ divider reduces a 5 V receiver DAT high level to about 3.21 V (or a measured 3.9 V high to about 2.51 V). Pico GPIO is not 5 V tolerant. If the receiver has an open collector output, add a 10 kΩ pull-up from GP16 to **3V3**, and omit the divider; check the module's actual output circuit first.

For the V1221 transmitter / V1222 receiver set, published instructions specify 5 V supply for both boards. If either board becomes hot, disconnect it immediately. Do not add a guessed series resistor to VCC or reconnect it to the Pico: first check the exact VCC/GND/DAT markings on that board, supply polarity, and for solder bridges or a short. A board that has overheated may be damaged.

The emitter module must accept a 3.3 V logic high at DAT. If it requires 5 V logic, add a 3.3 V to 5 V level shifter or a suitable transistor driver according to the module's input circuit. Never connect 5 V directly to GP17. Module documentation in `hardware.md` does not specify output stage or input threshold, so verify these before wiring.

The supplied module description mixes wavelength and carrier frequency units. This firmware assumes a **demodulating, active-low receiver** and a **38 kHz carrier**. Confirm the modules are compatible; a bare photodiode receiver will not work with this capture method.
