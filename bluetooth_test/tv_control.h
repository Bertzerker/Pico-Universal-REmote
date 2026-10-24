#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "btstack_tlv.h"

bool tv_connected(void);
bool tv_key(const char *name);
bool tv_mouse_move(int dx, int dy);
bool tv_text(const char *text, size_t length);
void tv_target_init(const btstack_tlv_t *tlv, void *context);
unsigned tv_target_get(void);
bool tv_target_select(unsigned target);
