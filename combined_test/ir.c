#include "ir.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#define IR_RX_GPIO 16
#define IR_TX_GPIO 17
#define IR_GAP_US 25000u
#define IR_TIMEOUT_US 10000000u

static uint16_t pwm_slice;
static uint16_t pwm_mark_level;
static volatile uint32_t edge_times[IR_MAX_EDGES];
static volatile unsigned edge_count;
static volatile uint32_t capture_started;
static volatile uint32_t last_edge;
static volatile bool capturing;
static volatile bool overflow;
static volatile bool frame_complete;

static void resume_capture(uint32_t now) {
    edge_count = 0;
    overflow = false;
    frame_complete = false;
    last_edge = now;
    capturing = true;
    gpio_set_irq_enabled(IR_RX_GPIO, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
}

static void on_gpio_edge(uint gpio, uint32_t events) {
    (void)events;
    if (gpio != IR_RX_GPIO || !capturing) return;
    uint32_t now = time_us_32();
    if (edge_count && now - last_edge > IR_GAP_US) {
        if (edge_count >= 4) {
            // Preserve the first full frame. A held remote button may start a
            // short repeat frame before the web page polls the result.
            frame_complete = true;
            capturing = false;
            return;
        }
        edge_count = 0;
    }
    if (edge_count == IR_MAX_EDGES) {
        overflow = true;
        capturing = false;
        return;
    }
    edge_times[edge_count++] = now;
    last_edge = now;
}

void ir_init(void) {
    gpio_init(IR_RX_GPIO);
    gpio_set_dir(IR_RX_GPIO, GPIO_IN);
    gpio_set_function(IR_TX_GPIO, GPIO_FUNC_PWM);
    pwm_slice = pwm_gpio_to_slice_num(IR_TX_GPIO);
    unsigned channel = pwm_gpio_to_channel(IR_TX_GPIO);
    uint32_t wrap = clock_get_hz(clk_sys) / 38000u - 1u;
    pwm_mark_level = (wrap + 1u) / 3u;
    pwm_set_wrap(pwm_slice, wrap);
    pwm_set_chan_level(pwm_slice, channel, 0);
    pwm_set_enabled(pwm_slice, true);
    gpio_set_irq_enabled_with_callback(IR_RX_GPIO, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE,
                                        false, on_gpio_edge);
}

bool ir_send(const uint16_t *durations, size_t count) {
    if (!durations || count < 3 || count >= IR_MAX_EDGES || !(count & 1)) return false;
    for (size_t i = 0; i < count; ++i)
        if (durations[i] < 80 || durations[i] > 20000) return false;
    gpio_set_irq_enabled(IR_RX_GPIO, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, false);
    unsigned channel = pwm_gpio_to_channel(IR_TX_GPIO);
    for (size_t i = 0; i < count; ++i) {
        pwm_set_chan_level(pwm_slice, channel, i & 1 ? 0 : pwm_mark_level);
        busy_wait_us_32(durations[i]);
    }
    pwm_set_chan_level(pwm_slice, channel, 0);
    return true;
}

void ir_capture_start(void) {
    edge_count = 0;
    overflow = false;
    frame_complete = false;
    capture_started = time_us_32();
    last_edge = capture_started;
    capturing = true;
    gpio_set_irq_enabled(IR_RX_GPIO, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
}

int ir_capture_status(uint16_t *durations, size_t *count) {
    if (!count || !durations) return -2;
    uint32_t now = time_us_32();
    if (overflow) {
        printf("IR capture overflow: %u edges\n", edge_count);
        gpio_set_irq_enabled(IR_RX_GPIO, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, false);
        resume_capture(now);
        return 0;
    }
    if (frame_complete || (capturing && edge_count >= 4 && now - last_edge > IR_GAP_US)) {
        capturing = false;
        gpio_set_irq_enabled(IR_RX_GPIO, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, false);
        unsigned n = edge_count - 1;
        if (!(n & 1)) --n;
        printf("IR frame: %u timings, first edges %lu %lu %lu %lu\n", n,
               (unsigned long)(edge_count > 0 ? edge_times[0] : 0),
               (unsigned long)(edge_count > 1 ? edge_times[1] - edge_times[0] : 0),
               (unsigned long)(edge_count > 2 ? edge_times[2] - edge_times[1] : 0),
               (unsigned long)(edge_count > 3 ? edge_times[3] - edge_times[2] : 0));
        if (n < 3) {
            resume_capture(now);
            return 0;
        }
        for (unsigned i = 0; i < n; ++i) {
            uint32_t duration = edge_times[i + 1] - edge_times[i];
            if (duration < 80 || duration > 20000) {
                printf("IR invalid duration %u at %u\n", (unsigned)duration, i);
                resume_capture(now);
                return 0;
            }
            durations[i] = duration;
        }
        if (n == 3 && durations[0] >= 8000 && durations[0] <= 10000 &&
                durations[1] >= 1600 && durations[1] <= 3000 &&
                durations[2] >= 300 && durations[2] <= 1000) {
            // NEC repeat frames carry no button code. Wait for a fresh press.
            resume_capture(now);
            return 0;
        }
        if (n < 10) {
            resume_capture(now);
            return 0;
        }
        *count = n;
        return 1;
    }
    if (now - capture_started > IR_TIMEOUT_US) {
        printf("IR timeout: %u edges\n", edge_count);
        capturing = false;
        gpio_set_irq_enabled(IR_RX_GPIO, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, false);
        return -1;
    }
    return 0;
}
