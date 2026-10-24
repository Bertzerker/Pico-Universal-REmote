#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IR_MAX_EDGES 600

void ir_init(void);
bool ir_send(const uint16_t *durations, size_t count);
void ir_capture_start(void);
/* 0 waiting, 1 complete, -1 timeout, -2 overflow or unclear signal. */
int ir_capture_status(uint16_t *durations, size_t *count);
