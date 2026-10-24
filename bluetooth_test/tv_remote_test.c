/* Temporary Pico 2 W Bluetooth HID test. No Wi-Fi or IR in this image. */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "btstack.h"
#include "btstack_stdin.h"
#include "ble/gatt-service/battery_service_server.h"
#include "ble/gatt-service/device_information_service_server.h"
#include "ble/gatt-service/hids_device.h"
#include "tv_remote_test.h"
#include "tv_control.h"
#include "pico/unique_id.h"

// Report 1: keyboard; report 2: five-button mouse; report 3: consumer usage.
static const uint8_t hid_descriptor[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x85, 0x01,
    0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x75, 0x08, 0x95, 0x01, 0x81, 0x03,
    0x75, 0x08, 0x95, 0x06, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xc0,

    0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x85, 0x02,
    0x09, 0x01, 0xa1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x05,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x05, 0x81, 0x02,
    0x75, 0x03, 0x95, 0x01, 0x81, 0x03,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06,
    0xc0, 0xc0,

    0x05, 0x0c, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x03,
    0x15, 0x00, 0x26, 0xff, 0x03, 0x19, 0x00, 0x2a, 0xff, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xc0,
};

static const uint8_t advertising_data[] = {
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    0x0d, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
    'P','i','c','o',' ','T','V',' ','T','e','s','t',
    0x03, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
    ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE & 0xff,
    ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE >> 8,
    0x03, BLUETOOTH_DATA_TYPE_APPEARANCE, 0xc1, 0x03,
};
static const uint8_t fire_advertising_data[] = {
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    0x0a, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
    'P','i','c','o',' ','F','i','r','e',
    0x03, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
    ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE & 0xff,
    ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE >> 8,
    0x03, BLUETOOTH_DATA_TYPE_APPEARANCE, 0xc1, 0x03,
};
#define TARGET_TAG 0x42545345u
static hci_con_handle_t connection = HCI_CON_HANDLE_INVALID;
static const btstack_tlv_t *target_store;
static void *target_store_context;
static unsigned selected_target;
static bool target_switching;
static bd_addr_t fire_local_address;

void tv_target_init(const btstack_tlv_t *tlv, void *context) {
    target_store = tlv;
    target_store_context = context;
    uint8_t saved = 0;
    if (tlv->get_tag(context, TARGET_TAG, &saved, 1) == 1 && saved <= 1)
        selected_target = saved;
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    for (unsigned i = 0; i < 6; ++i) fire_local_address[i] = board_id.id[i];
    fire_local_address[0] = (fire_local_address[0] & 0x3f) | 0xc0;
}

unsigned tv_target_get(void) { return selected_target; }

static void advertise_selected_target(void) {
    gap_advertisements_enable(0);
    if (selected_target == 0) {
        gap_random_address_set_mode(GAP_RANDOM_ADDRESS_TYPE_OFF);
        gap_advertisements_set_data(sizeof(advertising_data), (uint8_t *)advertising_data);
    } else {
        gap_random_address_set(fire_local_address);
        gap_advertisements_set_data(sizeof(fire_advertising_data), (uint8_t *)fire_advertising_data);
    }
    bd_addr_t no_address = {0};
    gap_advertisements_set_params(0x0030, 0x0030, 0, 0, no_address, 0x07, 0x00);
    gap_advertisements_enable(1);
    printf("Advertising for target %u\n", selected_target);
}

bool tv_target_select(unsigned target) {
    if (target > 1 || !target_store) return false;
    if (target == selected_target) return true;
    uint8_t value = target;
    if (target_store->store_tag(target_store_context, TARGET_TAG, &value, 1)) return false;
    selected_target = target;
    if (connection != HCI_CON_HANDLE_INVALID) {
        target_switching = true;
        gap_disconnect(connection);
    } else advertise_selected_target();
    return true;
}

static btstack_packet_callback_registration_t hci_handler;
static btstack_packet_callback_registration_t sm_handler;
static hids_device_report_t report_storage[3];
static uint8_t protocol_mode = 1;
static uint8_t subscribed[4];
static uint8_t boot_keyboard_subscribed;
static uint8_t boot_mouse_subscribed;
static uint8_t active_report;
static uint8_t keycode;
static uint8_t key_modifier;
static uint8_t mouse_buttons;
static int8_t mouse_dx;
static int8_t mouse_dy;
static uint16_t consumer_usage;
static uint8_t phase; // 0 idle, 1 key-down pending, 2 key-up pending
static btstack_timer_source_t release_timer;
static char serial_line[82];
static uint8_t serial_length;
static char serial_command;
static char text_buffer[81];
static uint8_t text_length;
static uint8_t text_position;
static uint8_t text_active;

static void request_send(void) {
    if (connection != HCI_CON_HANDLE_INVALID) {
        hids_device_request_can_send_now_event(connection);
    }
}

static void release_timer_handler(btstack_timer_source_t *timer) {
    (void) timer;
    phase = 2;
    request_send();
}

static void send_report(uint8_t released) {
    uint8_t keyboard[8] = {released ? 0 : key_modifier, 0, released ? 0 : keycode, 0, 0, 0, 0, 0};
    uint8_t mouse[3] = {released ? 0 : mouse_buttons,
                        released ? 0 : (uint8_t)mouse_dx,
                        released ? 0 : (uint8_t)mouse_dy};
    uint16_t usage = released ? 0 : consumer_usage;
    uint8_t consumer[2] = {(uint8_t) usage, (uint8_t)(usage >> 8)};
    uint8_t status = 0;
    if (active_report == 1) {
        status = protocol_mode == 0
            ? hids_device_send_boot_keyboard_input_report(connection, keyboard, sizeof(keyboard))
            : hids_device_send_input_report_for_id(connection, 1, keyboard, sizeof(keyboard));
    } else if (active_report == 2) {
        if (protocol_mode == 0 && mouse_buttons > 7) {
            printf("Mouse button 4/5 requires report mode\n");
            return;
        }
        status = protocol_mode == 0
            ? hids_device_send_boot_mouse_input_report(connection, mouse, sizeof(mouse))
            : hids_device_send_input_report_for_id(connection, 2, mouse, sizeof(mouse));
    } else if (active_report == 3) {
        if (protocol_mode == 0) {
            printf("Consumer controls require report mode\n");
            return;
        }
        status = hids_device_send_input_report_for_id(connection, 3, consumer, sizeof(consumer));
    }
    printf("report=%u %s status=%u\n", active_report, released ? "up" : "down", status);
}

static void start_action(uint8_t report_id, uint8_t key, uint8_t buttons, uint16_t usage) {
    if (connection == HCI_CON_HANDLE_INVALID || phase != 0) {
        printf("Not connected or busy\n");
        return;
    }
    if (protocol_mode != 0 && !subscribed[report_id]) {
        printf("Report %u not subscribed\n", report_id);
        return;
    }
    active_report = report_id;
    keycode = key;
    key_modifier = 0;
    mouse_buttons = buttons;
    mouse_dx = 0;
    mouse_dy = 0;
    consumer_usage = usage;
    phase = 1;
    request_send();
}

static int ascii_key(char ch, uint8_t *key, uint8_t *modifier) {
    *modifier = 0;
    if (ch >= 'a' && ch <= 'z') { *key = 0x04 + ch - 'a'; return 1; }
    if (ch >= 'A' && ch <= 'Z') { *key = 0x04 + ch - 'A'; *modifier = 0x02; return 1; }
    if (ch >= '1' && ch <= '9') { *key = 0x1e + ch - '1'; return 1; }
    if (ch == '0') { *key = 0x27; return 1; }
    if (ch == ' ') { *key = 0x2c; return 1; }
    if (ch == '\n') { *key = 0x28; return 1; }
    const char *plain = "-=[]\\;',./`";
    const char *shifted = "_+{}|:\"<>?~";
    const uint8_t codes[] = {0x2d,0x2e,0x2f,0x30,0x31,0x33,0x34,0x36,0x37,0x38,0x35};
    for (unsigned i = 0; i < sizeof(codes); ++i) {
        if (ch == plain[i] || ch == shifted[i]) {
            *key = codes[i];
            *modifier = ch == shifted[i] ? 0x02 : 0;
            return 1;
        }
    }
    const char *shifted_digits = "!@#$%^&*()";
    for (unsigned i = 0; i < 10; ++i) {
        if (ch == shifted_digits[i]) {
            *key = i == 9 ? 0x27 : 0x1e + i;
            *modifier = 0x02;
            return 1;
        }
    }
    return 0;
}

static void send_next_text_char(void) {
    if (text_position >= text_length) {
        text_active = 0;
        printf("Text complete\n");
        return;
    }
    uint8_t key, modifier;
    if (!ascii_key(text_buffer[text_position++], &key, &modifier)) {
        text_active = 0;
        printf("Unsupported text character\n");
        return;
    }
    start_action(1, key, 0, 0);
    key_modifier = modifier;
}

static void process_serial_line(void) {
    serial_line[serial_length] = '\0';
    if (serial_command == 'M') {
        int dx, dy;
        if (sscanf(serial_line, "%d,%d", &dx, &dy) != 2 ||
                dx < -127 || dx > 127 || dy < -127 || dy > 127) {
            printf("Use M<dx>,<dy> with -127..127\n");
        } else {
            start_action(2, 0, 0, 0);
            if (phase == 1) { mouse_dx = dx; mouse_dy = dy; }
        }
    } else if (serial_command == 'T') {
        if (!serial_length) {
            printf("Text is empty\n");
        } else if (connection == HCI_CON_HANDLE_INVALID ||
                   (protocol_mode != 0 && !subscribed[1])) {
            printf("Keyboard not connected\n");
        } else if (phase != 0 || text_active) {
            printf("Busy\n");
        } else {
            for (unsigned i = 0; i < serial_length; ++i) {
                uint8_t key, modifier;
                if (!ascii_key(serial_line[i], &key, &modifier)) {
                    printf("Unsupported text character at %u\n", i);
                    return;
                }
            }
            memcpy(text_buffer, serial_line, serial_length);
            text_length = serial_length;
            text_position = 0;
            text_active = 1;
            send_next_text_char();
        }
    }
}

static void stdin_process(char c) {
    if (serial_command) {
        if (c == '\n' || c == '\r') {
            process_serial_line();
            serial_command = 0;
            serial_length = 0;
        } else if (serial_length < sizeof(serial_line) - 1) {
            serial_line[serial_length++] = c;
        } else {
            serial_command = 0;
            serial_length = 0;
            printf("Serial command too long\n");
        }
        return;
    }
    if (c == 'M' || c == 'T') {
        serial_command = c;
        serial_length = 0;
        return;
    }
    switch (c) {
        case '?':
            printf("BLE handle=0x%04x mode=%u reports=%u,%u,%u boot=%u,%u phase=%u\n",
                   connection, protocol_mode, subscribed[1], subscribed[2], subscribed[3],
                   boot_keyboard_subscribed, boot_mouse_subscribed, phase);
            break;
        case 'w': start_action(1, 0x52, 0, 0); break;
        case 'a': start_action(1, 0x50, 0, 0); break;
        case 's': start_action(1, 0x51, 0, 0); break;
        case 'd': start_action(1, 0x4f, 0, 0); break;
        case 'e': start_action(1, 0x28, 0, 0); break;
        case 'k': start_action(1, 0x29, 0, 0); break;
        case 'b': start_action(2, 0, 0x08, 0); break;
        case 'l': start_action(2, 0, 0x01, 0); break;
        case 'h': start_action(3, 0, 0, 0x0223); break;
        case '+': start_action(3, 0, 0, 0x00e9); break;
        case '-': start_action(3, 0, 0, 0x00ea); break;
        case 'm': start_action(3, 0, 0, 0x00e2); break;
        case 't': start_action(3, 0, 0, 0x00cd); break;
        case 'g': start_action(3, 0, 0, 0x008d); break;
        case 'c': start_action(3, 0, 0, 0x009c); break;
        case 'v': start_action(3, 0, 0, 0x009d); break;
        case 'x': start_action(3, 0, 0, 0x0061); break;
        case 'p': start_action(3, 0, 0, 0x0030); break;
        default: break;
    }
}

bool tv_connected(void) {
    return !target_switching && connection != HCI_CON_HANDLE_INVALID &&
           subscribed[1] && subscribed[2] && subscribed[3];
}

bool tv_key(const char *name) {
    if (!tv_connected() || phase || text_active) return false;
    if (strcmp(name, "up") == 0) start_action(1, 0x52, 0, 0);
    else if (strcmp(name, "down") == 0) start_action(1, 0x51, 0, 0);
    else if (strcmp(name, "left") == 0) start_action(1, 0x50, 0, 0);
    else if (strcmp(name, "right") == 0) start_action(1, 0x4f, 0, 0);
    else if (strcmp(name, "select") == 0) start_action(1, 0x28, 0, 0);
    else if (strcmp(name, "back") == 0) start_action(2, 0, 0x08, 0);
    else if (strcmp(name, "mouse_click") == 0) start_action(2, 0, 0x01, 0);
    else if (strcmp(name, "home") == 0) start_action(3, 0, 0, 0x0223);
    else if (strcmp(name, "volume_up") == 0) start_action(3, 0, 0, 0x00e9);
    else if (strcmp(name, "volume_down") == 0) start_action(3, 0, 0, 0x00ea);
    else if (strcmp(name, "mute") == 0) start_action(3, 0, 0, 0x00e2);
    else if (strcmp(name, "play_pause") == 0) start_action(3, 0, 0, 0x00cd);
    else return false;
    return true;
}

bool tv_mouse_move(int dx, int dy) {
    if (!tv_connected() || phase || text_active || dx < -127 || dx > 127 ||
            dy < -127 || dy > 127) return false;
    active_report = 2;
    keycode = 0;
    key_modifier = 0;
    mouse_buttons = 0;
    consumer_usage = 0;
    mouse_dx = dx;
    mouse_dy = dy;
    phase = 1;
    request_send();
    return true;
}

bool tv_text(const char *text, size_t length) {
    if (!tv_connected() || phase || text_active || length < 1 || length > 80)
        return false;
    for (size_t i = 0; i < length; ++i) {
        uint8_t key, modifier;
        if (!ascii_key(text[i], &key, &modifier)) return false;
    }
    memcpy(text_buffer, text, length);
    text_length = length;
    text_position = 0;
    text_active = 1;
    send_next_text_char();
    return true;
}

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void) channel;
    (void) size;
    if (packet_type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)) {
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            printf("Disconnected reason=0x%02x\n", hci_event_disconnection_complete_get_reason(packet));
            connection = HCI_CON_HANDLE_INVALID;
            memset(subscribed, 0, sizeof(subscribed));
            boot_keyboard_subscribed = 0;
            boot_mouse_subscribed = 0;
            phase = 0;
            text_active = 0;
            target_switching = false;
            advertise_selected_target();
            break;
        case HCI_EVENT_LE_META:
            switch (hci_event_le_meta_get_subevent_code(packet)) {
                case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
                    if (hci_subevent_le_connection_complete_get_status(packet) == 0) {
                        connection = hci_subevent_le_connection_complete_get_connection_handle(packet);
                        printf("BLE connected handle=0x%04x\n", connection);
                    }
                    break;
                case HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE_V1:
                    if (hci_subevent_le_enhanced_connection_complete_v1_get_status(packet) == 0) {
                        connection = hci_subevent_le_enhanced_connection_complete_v1_get_connection_handle(packet);
                        printf("BLE connected v1 handle=0x%04x\n", connection);
                    }
                    break;
                case HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE_V2:
                    if (hci_subevent_le_enhanced_connection_complete_v2_get_status(packet) == 0) {
                        connection = hci_subevent_le_enhanced_connection_complete_v2_get_connection_handle(packet);
                        printf("BLE connected v2 handle=0x%04x\n", connection);
                    }
                    break;
                default: break;
            }
            break;
        case SM_EVENT_PAIRING_COMPLETE:
            printf("Pairing complete status=0x%02x\n", sm_event_pairing_complete_get_status(packet));
            break;
        case SM_EVENT_JUST_WORKS_REQUEST:
            sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
            break;
        case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
            sm_numeric_comparison_confirm(sm_event_numeric_comparison_request_get_handle(packet));
            break;
        case HCI_EVENT_HIDS_META:
            switch (hci_event_hids_meta_get_subevent_code(packet)) {
                case HIDS_SUBEVENT_INPUT_REPORT_ENABLE: {
                    uint8_t id = hids_subevent_input_report_enable_get_report_id(packet);
                    if (id < sizeof(subscribed)) subscribed[id] = hids_subevent_input_report_enable_get_enable(packet);
                    connection = hids_subevent_input_report_enable_get_con_handle(packet);
                    printf("Report %u subscribed=%u\n", id, id < sizeof(subscribed) ? subscribed[id] : 0);
                    break;
                }
                case HIDS_SUBEVENT_BOOT_KEYBOARD_INPUT_REPORT_ENABLE:
                    connection = hids_subevent_boot_keyboard_input_report_enable_get_con_handle(packet);
                    boot_keyboard_subscribed = hids_subevent_boot_keyboard_input_report_enable_get_enable(packet);
                    printf("Boot keyboard subscribed=%u\n", boot_keyboard_subscribed);
                    break;
                case HIDS_SUBEVENT_BOOT_MOUSE_INPUT_REPORT_ENABLE:
                    connection = hids_subevent_boot_mouse_input_report_enable_get_con_handle(packet);
                    boot_mouse_subscribed = hids_subevent_boot_mouse_input_report_enable_get_enable(packet);
                    printf("Boot mouse subscribed=%u\n", boot_mouse_subscribed);
                    break;
                case HIDS_SUBEVENT_PROTOCOL_MODE:
                    protocol_mode = hids_subevent_protocol_mode_get_protocol_mode(packet);
                    printf("Protocol mode=%u\n", protocol_mode);
                    break;
                case HIDS_SUBEVENT_CAN_SEND_NOW:
                    if (phase == 1) {
                        send_report(0);
                        release_timer.process = release_timer_handler;
                        btstack_run_loop_set_timer(&release_timer, 60);
                        btstack_run_loop_add_timer(&release_timer);
                    } else if (phase == 2) {
                        send_report(1);
                        phase = 0;
                        if (text_active) send_next_text_char();
                    }
                    break;
                default: break;
            }
            break;
        default: break;
    }
}

int btstack_main(int argc, const char *argv[]) {
    (void) argc;
    (void) argv;
    l2cap_init();
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_SECURE_CONNECTION | SM_AUTHREQ_BONDING);
    att_server_init(profile_data, NULL, NULL);
    battery_service_server_init(100);
    device_information_service_server_init();
    hids_device_init_with_storage(0, hid_descriptor, sizeof(hid_descriptor), 3, report_storage);

    advertise_selected_target();

    hci_handler.callback = packet_handler;
    hci_add_event_handler(&hci_handler);
    sm_handler.callback = packet_handler;
    sm_add_event_handler(&sm_handler);
    hids_device_register_packet_handler(packet_handler);
    btstack_stdin_setup(stdin_process);
    printf("Pico TV Test: w/a/s/d/e/b/l/h/+/-/m/t/g/c/v/x/p; Mdx,dy\\n; Ttext\\n\n");
    hci_power_control(HCI_POWER_ON);
    return 0;
}
