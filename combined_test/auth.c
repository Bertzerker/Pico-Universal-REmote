#include "auth.h"
#include <stdint.h>
#include <string.h>
#include "pico/sha256.h"
#include "pico/rand.h"
#include "local_settings.h"

#define MAX_SESSIONS 6
static char active_tokens[MAX_SESSIONS][33];
static unsigned next_session;

static uint8_t hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return 0xff;
}

static bool decode_hex(const char *text, uint8_t *out, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        uint8_t high = hex_digit(text[i*2]);
        uint8_t low = hex_digit(text[i*2+1]);
        if (high > 15 || low > 15) return false;
        out[i] = high << 4 | low;
    }
    return true;
}

static void sha256(const uint8_t *input, size_t length, uint8_t output[32]) {
    pico_sha256_state_t state;
    sha256_result_t result;
    pico_sha256_start_blocking(&state, SHA256_BIG_ENDIAN, false);
    pico_sha256_update_blocking(&state, input, length);
    pico_sha256_finish(&state, &result);
    memcpy(output, result.bytes, 32);
}

bool auth_self_test(void) {
    static const uint8_t expected[32] = {
        0xb5,0xdf,0x66,0x1d,0x56,0x41,0x2f,0x49,0xe6,0x25,0x03,0xa1,0x42,0x7b,0x30,0x32,
        0x54,0x9c,0x4f,0xe5,0x51,0x6e,0x2e,0xb2,0x5c,0x0d,0x99,0xe0,0x07,0xd1,0xb2,0x62
    };
    static const char password[] = "testpassword";
    uint8_t input[16 + sizeof(password) - 1];
    uint8_t digest[32];
    for (unsigned i = 0; i < 16; ++i) input[i] = i;
    memcpy(input + 16, password, sizeof(password) - 1);
    sha256(input, sizeof(input), digest);
    for (unsigned i = 1; i < 1000; ++i) sha256(digest, 32, digest);
    return memcmp(digest, expected, 32) == 0;
}

bool auth_login(const char *password, size_t length, char token_out[33]) {
    if (length < 10 || length > 128) return false;
    uint8_t input[16 + 128];
    uint8_t expected[32], digest[32];
    if (!decode_hex(LOCAL_PASSWORD_SALT, input, 16) ||
            !decode_hex(LOCAL_PASSWORD_HASH, expected, 32)) return false;
    memcpy(input + 16, password, length);
    sha256(input, 16 + length, digest);
    for (unsigned i = 1; i < 1000; ++i) sha256(digest, sizeof(digest), digest);
    uint8_t mismatch = 0;
    for (unsigned i = 0; i < 32; ++i) mismatch |= digest[i] ^ expected[i];
    if (mismatch) return false;
    rng_128_t random;
    get_rand_128(&random);
    const uint8_t *bytes = (const uint8_t *)&random;
    static const char alphabet[] = "0123456789abcdef";
    char *active_token = active_tokens[next_session];
    next_session = (next_session + 1) % MAX_SESSIONS;
    for (unsigned i = 0; i < 16; ++i) {
        active_token[2*i] = alphabet[bytes[i] >> 4];
        active_token[2*i+1] = alphabet[bytes[i] & 15];
    }
    active_token[32] = 0;
    memcpy(token_out, active_token, 33);
    return true;
}

bool auth_token_valid(const char *token, size_t length) {
    if (length != 32) return false;
    for (unsigned session = 0; session < MAX_SESSIONS; ++session) {
        uint8_t mismatch = !active_tokens[session][0];
        for (unsigned i = 0; i < 32; ++i)
            mismatch |= token[i] ^ active_tokens[session][i];
        if (!mismatch) return true;
    }
    return false;
}
