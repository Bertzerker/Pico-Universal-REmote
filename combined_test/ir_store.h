#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "btstack_tlv.h"

void ir_store_init(const btstack_tlv_t *tlv, void *context);
const char *ir_store_catalog(void);
bool ir_store_send(unsigned remote, unsigned button);
bool ir_store_create(const char *name, unsigned rows, unsigned cols);
bool ir_store_rename(unsigned remote, const char *name);
bool ir_store_resize(unsigned remote, unsigned rows, unsigned cols);
bool ir_store_button(unsigned remote, unsigned button, const char *name, const char *color);
bool ir_store_delete(unsigned remote);
bool ir_store_learn_start(unsigned remote, unsigned button);
/* 0 waiting, 1 learned and saved, -1 timed out, -2 invalid signal, -3 storage full. */
int ir_store_learn_status(unsigned *timing_count);
const char *ir_store_code(unsigned remote, unsigned button);
const char *ir_store_last_code(void);
