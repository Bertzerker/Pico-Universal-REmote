#include "ir_store.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir.h"
#include "learned_ir.h"

#define IR_TAG 0x49524442u
#define MAX_REMOTES 6
#define MAX_BUTTONS 64
#define MAX_NAME 30
#define MAX_BLOB 3700

typedef struct {
    char name[MAX_NAME + 1];
    uint8_t color[3];
    uint16_t count;
    uint16_t *signal;
} button_t;
typedef struct {
    char name[MAX_NAME + 1];
    uint8_t rows, cols;
    button_t buttons[MAX_BUTTONS];
} remote_t;

static remote_t remotes[MAX_REMOTES];
static unsigned remote_count;
static const btstack_tlv_t *store;
static void *store_context;
static uint8_t blob[MAX_BLOB];
static char catalog[30000];
static char code_json[5000];
static bool learning;
static unsigned learn_remote, learn_button;
static bool last_code_valid;
static unsigned last_remote, last_button;

static void clear_all(void) {
    for (unsigned r = 0; r < remote_count; ++r)
        for (unsigned b = 0; b < MAX_BUTTONS; ++b) free(remotes[r].buttons[b].signal);
    memset(remotes, 0, sizeof(remotes));
    remote_count = 0;
}

static bool valid_name(const char *name) {
    size_t n = strlen(name);
    if (!n || n > MAX_NAME) return false;
    for (size_t i = 0; i < n; ++i)
        if ((unsigned char)name[i] < 32 || (unsigned char)name[i] > 126) return false;
    return true;
}

static bool valid_size(unsigned rows, unsigned cols) {
    return rows >= 1 && rows <= 8 && cols >= 1 && cols <= 8 && rows * cols <= MAX_BUTTONS;
}

static void default_button(button_t *button, unsigned index) {
    snprintf(button->name, sizeof(button->name), "%u", index + 1);
    button->color[0] = 0x55;
    button->color[1] = 0xd6;
    button->color[2] = 0xbe;
}

static void load_originals(void) {
    for (unsigned r = 0; r < ir_remote_count && r < MAX_REMOTES; ++r) {
        const ir_remote_t *source = &ir_remotes[r];
        remote_t *target = &remotes[remote_count++];
        snprintf(target->name, sizeof(target->name), "%s", source->name);
        target->cols = source->cols;
        target->rows = (source->count + source->cols - 1) / source->cols;
        if (!valid_size(target->rows, target->cols)) { target->rows = 1; target->cols = 1; }
        unsigned limit = source->count < MAX_BUTTONS ? source->count : MAX_BUTTONS;
        for (unsigned b = 0; b < target->rows * target->cols; ++b) {
            button_t *out = &target->buttons[b];
            default_button(out, b);
            if (b >= limit) continue;
            snprintf(out->name, sizeof(out->name), "%s", source->buttons[b].name);
            out->count = source->buttons[b].count;
            if (out->count) {
                out->signal = malloc(out->count * sizeof(uint16_t));
                if (out->signal) memcpy(out->signal, source->buttons[b].signal, out->count * sizeof(uint16_t));
                else out->count = 0;
            }
        }
    }
}

static bool take(unsigned *offset, void *data, unsigned length, unsigned total) {
    if (*offset > total || length > total - *offset) return false;
    memcpy(data, blob + *offset, length);
    *offset += length;
    return true;
}

static bool load_saved(void) {
    int length = store->get_tag(store_context, IR_TAG, blob, sizeof(blob));
    if (length < 4 || blob[0] != 'I' || blob[1] != 'R' ||
            (blob[2] != 1 && blob[2] != 2) || blob[3] > MAX_REMOTES)
        return false;
    unsigned offset = 4;
    unsigned count = blob[3];
    for (unsigned r = 0; r < count; ++r) {
        remote_t *remote = &remotes[remote_count++];
        uint8_t metadata[3];
        if (!take(&offset, metadata, 3, length)) return false;
        remote->rows = metadata[0]; remote->cols = metadata[1];
        if (!valid_size(remote->rows, remote->cols) || !metadata[2] || metadata[2] > MAX_NAME ||
                !take(&offset, remote->name, metadata[2], length)) return false;
        remote->name[metadata[2]] = 0;
        for (unsigned b = 0; b < remote->rows * remote->cols; ++b) {
            button_t *button = &remote->buttons[b];
            uint8_t name_len;
            if (!take(&offset, &name_len, 1, length) || !name_len || name_len > MAX_NAME ||
                    !take(&offset, button->name, name_len, length) ||
                    !take(&offset, button->color, 3, length) ||
                    !take(&offset, &button->count, 2, length)) return false;
            button->name[name_len] = 0;
            if (button->count > IR_MAX_EDGES || (button->count && (!(button->count & 1) || button->count < 3))) return false;
            if (button->count) {
                button->signal = malloc(button->count * 2);
                if (!button->signal) return false;
                if (blob[2] == 1) {
                    if (!take(&offset, button->signal, button->count * 2, length)) return false;
                } else {
                    for (unsigned i = 0; i < button->count; ++i) {
                        uint8_t encoded;
                        if (!take(&offset, &encoded, 1, length)) return false;
                        if (encoded == 255) {
                            if (!take(&offset, &button->signal[i], 2, length)) return false;
                        } else {
                            button->signal[i] = encoded * 10u;
                        }
                    }
                }
            }
        }
    }
    return offset == (unsigned)length;
}

static void reload(void) {
    clear_all();
    last_code_valid = false;
    if (!load_saved()) { clear_all(); load_originals(); }
}

void ir_store_init(const btstack_tlv_t *tlv, void *context) {
    store = tlv;
    store_context = context;
    reload();
}

static bool put(unsigned *offset, const void *data, unsigned length) {
    if (*offset > MAX_BLOB || length > MAX_BLOB - *offset) return false;
    memcpy(blob + *offset, data, length);
    *offset += length;
    return true;
}

static bool save(void) {
    unsigned offset = 0;
    uint8_t header[] = {'I','R',2,(uint8_t)remote_count};
    if (!put(&offset, header, sizeof(header))) return false;
    for (unsigned r = 0; r < remote_count; ++r) {
        remote_t *remote = &remotes[r];
        uint8_t meta[] = {remote->rows, remote->cols, (uint8_t)strlen(remote->name)};
        if (!put(&offset, meta, sizeof(meta)) || !put(&offset, remote->name, meta[2])) return false;
        for (unsigned b = 0; b < remote->rows * remote->cols; ++b) {
            button_t *button = &remote->buttons[b];
            uint8_t name_len = strlen(button->name);
            if (!put(&offset, &name_len, 1) || !put(&offset, button->name, name_len) ||
                    !put(&offset, button->color, 3) || !put(&offset, &button->count, 2)) return false;
            for (unsigned i = 0; i < button->count; ++i) {
                uint16_t duration = button->signal[i];
                if (duration < 2545) {
                    uint8_t encoded = (duration + 5u) / 10u;
                    if (!put(&offset, &encoded, 1)) return false;
                } else {
                    uint8_t escape = 255;
                    if (!put(&offset, &escape, 1) || !put(&offset, &duration, 2)) return false;
                }
            }
        }
    }
    int result = store->store_tag(store_context, IR_TAG, blob, offset);
    printf("IR save: %u bytes, result %d\n", offset, result);
    return result == 0;
}

static bool commit(void) {
    if (save()) return true;
    reload();
    return false;
}

static bool append(const char *text, size_t length) {
    size_t used = strlen(catalog);
    if (used + length >= sizeof(catalog)) return false;
    memcpy(catalog + used, text, length);
    catalog[used + length] = 0;
    return true;
}

static bool literal(const char *text) { return append(text, strlen(text)); }

static bool json_string(const char *text) {
    if (!literal("\"")) return false;
    for (const char *p = text; *p; ++p) {
        if (*p == '"' || *p == '\\') { if (!literal("\\")) return false; }
        if (!append(p, 1)) return false;
    }
    return literal("\"");
}

const char *ir_store_catalog(void) {
    catalog[0] = 0;
    if (!literal("[")) return "[]";
    for (unsigned r = 0; r < remote_count; ++r) {
        remote_t *remote = &remotes[r];
        char number[16];
        if ((r && !literal(",")) || !literal("{\"name\":") || !json_string(remote->name) ||
                !literal(",\"rows\":")) return "[]";
        snprintf(number, sizeof(number), "%u", remote->rows);
        if (!literal(number) || !literal(",\"cols\":")) return "[]";
        snprintf(number, sizeof(number), "%u", remote->cols);
        if (!literal(number) || !literal(",\"buttons\":[")) return "[]";
        for (unsigned b = 0; b < remote->rows * remote->cols; ++b) {
            button_t *button = &remote->buttons[b];
            char details[48];
            snprintf(details, sizeof(details), ",\"learned\":%s,\"color\":\"#%02x%02x%02x\"}",
                     button->signal ? "true" : "false", button->color[0], button->color[1], button->color[2]);
            if ((b && !literal(",")) || !literal("{\"name\":") || !json_string(button->name) ||
                    !literal(details)) return "[]";
        }
        if (!literal("]}")) return "[]";
    }
    if (!literal("]")) return "[]";
    return catalog;
}

bool ir_store_send(unsigned remote, unsigned button) {
    if (remote >= remote_count || button >= remotes[remote].rows * remotes[remote].cols) return false;
    button_t *chosen = &remotes[remote].buttons[button];
    return chosen->signal && ir_send(chosen->signal, chosen->count);
}

bool ir_store_create(const char *name, unsigned rows, unsigned cols) {
    if (remote_count >= MAX_REMOTES || !valid_name(name) || !valid_size(rows, cols)) return false;
    remote_t *remote = &remotes[remote_count++];
    memset(remote, 0, sizeof(*remote));
    strcpy(remote->name, name);
    remote->rows = rows; remote->cols = cols;
    for (unsigned b = 0; b < rows * cols; ++b) default_button(&remote->buttons[b], b);
    return commit();
}

bool ir_store_rename(unsigned remote, const char *name) {
    if (remote >= remote_count || !valid_name(name)) return false;
    strcpy(remotes[remote].name, name);
    return commit();
}

bool ir_store_resize(unsigned remote, unsigned rows, unsigned cols) {
    if (remote >= remote_count || !valid_size(rows, cols)) return false;
    remote_t *target = &remotes[remote];
    unsigned old_count = target->rows * target->cols, new_count = rows * cols;
    for (unsigned b = new_count; b < old_count; ++b) {
        free(target->buttons[b].signal);
        memset(&target->buttons[b], 0, sizeof(button_t));
    }
    for (unsigned b = old_count; b < new_count; ++b) default_button(&target->buttons[b], b);
    target->rows = rows; target->cols = cols;
    return commit();
}

static int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool ir_store_button(unsigned remote, unsigned button, const char *name, const char *color) {
    if (remote >= remote_count || button >= remotes[remote].rows * remotes[remote].cols ||
            !valid_name(name) || strlen(color) != 7 || color[0] != '#') return false;
    uint8_t rgb[3];
    for (unsigned i = 0; i < 3; ++i) {
        int high = hex_value(color[1+i*2]), low = hex_value(color[2+i*2]);
        if (high < 0 || low < 0) return false;
        rgb[i] = high * 16 + low;
    }
    button_t *target = &remotes[remote].buttons[button];
    strcpy(target->name, name);
    memcpy(target->color, rgb, 3);
    return commit();
}

bool ir_store_delete(unsigned remote) {
    if (remote >= remote_count) return false;
    for (unsigned b = 0; b < MAX_BUTTONS; ++b) free(remotes[remote].buttons[b].signal);
    for (unsigned r = remote + 1; r < remote_count; ++r) remotes[r-1] = remotes[r];
    memset(&remotes[--remote_count], 0, sizeof(remote_t));
    last_code_valid = false;
    return commit();
}

bool ir_store_learn_start(unsigned remote, unsigned button) {
    if (learning || remote >= remote_count || button >= remotes[remote].rows * remotes[remote].cols) return false;
    learn_remote = remote; learn_button = button;
    learning = true;
    ir_capture_start();
    return true;
}

int ir_store_learn_status(unsigned *timing_count) {
    if (!learning) return -2;
    static uint16_t capture[IR_MAX_EDGES];
    size_t count = 0;
    int status = ir_capture_status(capture, &count);
    if (!status) return 0;
    printf("IR learn: capture status %d, timings %u\n", status, (unsigned)count);
    learning = false;
    if (status < 0) return status;
    button_t *target = &remotes[learn_remote].buttons[learn_button];
    uint16_t *signal = malloc(count * 2);
    if (!signal) return -3;
    memcpy(signal, capture, count * 2);
    free(target->signal);
    target->signal = signal;
    target->count = count;
    if (!commit()) return -3;
    last_remote = learn_remote;
    last_button = learn_button;
    last_code_valid = true;
    if (timing_count) *timing_count = count;
    return 1;
}

const char *ir_store_code(unsigned remote, unsigned button) {
    if (remote >= remote_count || button >= remotes[remote].rows * remotes[remote].cols)
        return NULL;
    button_t *chosen = &remotes[remote].buttons[button];
    int used = snprintf(code_json, sizeof(code_json),
                        "{\"remote\":%u,\"button\":%u,\"signal\":[", remote, button);
    for (unsigned i = 0; i < chosen->count; ++i) {
        if (used < 0 || used >= sizeof(code_json) - 12) return NULL;
        used += snprintf(code_json + used, sizeof(code_json) - used,
                         "%s%u", i ? "," : "", chosen->signal[i]);
    }
    if (used < 0 || used + 3 > sizeof(code_json)) return NULL;
    memcpy(code_json + used, "]}", 3);
    return code_json;
}

const char *ir_store_last_code(void) {
    return last_code_valid ? ir_store_code(last_remote, last_button) : "{\"signal\":[]}";
}
