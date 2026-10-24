"""Receive demodulated IR envelopes and replay them with a 38 kHz carrier."""

import time
from machine import Pin, PWM
import micropython

micropython.alloc_emergency_exception_buf(100)


class IR:
    MAX_EDGES = 600
    GAP_US = 25000

    def __init__(self, rx_pin, tx_pin, carrier_hz=38000):
        self.rx = Pin(rx_pin, Pin.IN)
        self.tx = PWM(Pin(tx_pin), freq=carrier_hz, duty_u16=0)
        self.edges = [0] * self.MAX_EDGES
        self.count = 0
        self.last = 0
        self.overflow = False
        self.listening = False

    def _edge(self, pin):
        if not self.listening:
            return
        now = time.ticks_us()
        if self.count and time.ticks_diff(now, self.last) > self.GAP_US:
            self.count = 0
        if self.count == self.MAX_EDGES:
            self.overflow = True
            self.listening = False
            return
        self.edges[self.count] = now
        self.count += 1
        self.last = now

    def learn(self, timeout_ms=10000):
        self.count = 0
        self.overflow = False
        self.listening = True
        self.rx.irq(self._edge, Pin.IRQ_FALLING | Pin.IRQ_RISING)
        start = time.ticks_ms()
        try:
            while time.ticks_diff(time.ticks_ms(), start) < timeout_ms:
                if self.overflow:
                    raise ValueError("Signal too long; try a shorter press")
                if self.count >= 4 and time.ticks_diff(time.ticks_us(), self.last) > self.GAP_US:
                    break
                time.sleep_ms(5)
        finally:
            self.listening = False
            self.rx.irq(handler=None)
        count = self.count
        if count < 4:
            raise ValueError("No IR signal received")
        # First falling edge starts a mark on active-low demodulated receivers.
        durations = [time.ticks_diff(self.edges[i + 1], self.edges[i]) for i in range(count - 1)]
        if len(durations) % 2 == 0:
            durations.pop()  # End on a mark; trailing space is not part of the code.
        if len(durations) < 3 or any(x < 80 or x > 20000 for x in durations):
            raise ValueError("Unclear IR signal; retry learning")
        return durations

    def send(self, durations):
        if not durations or len(durations) > self.MAX_EDGES or len(durations) % 2 != 1:
            raise ValueError("Invalid signal")
        self.rx.irq(handler=None)
        try:
            for i, duration in enumerate(durations):
                if not isinstance(duration, int) or duration < 80 or duration > 20000:
                    raise ValueError("Invalid signal timing")
                self.tx.duty_u16(21845 if i % 2 == 0 else 0)
                time.sleep_us(duration)
        finally:
            self.tx.duty_u16(0)
