#pragma once
#include <stdbool.h>
#include <stddef.h>

bool auth_login(const char *password, size_t length, char token_out[33]);
bool auth_token_valid(const char *token, size_t length);
bool auth_self_test(void);
