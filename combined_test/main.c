/* Coexistence test: the proven BLE HID plus the existing router connection. */
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "btstack.h"
#include "btstack_tlv_flash_bank.h"
#include "ble/le_device_db_tlv.h"
#include "pico/btstack_flash_bank.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "local_settings.h"
#include "dhcpserver.h"
#include "dnsserver.h"
#include "ir.h"
#include "ir_store.h"
#include "auth.h"
#include "tv_control.h"

int btstack_main(int argc, const char *argv[]);
bool http_server_start(void);
static btstack_tlv_flash_bank_t bond_storage;
static dhcp_server_t dhcp_server;
static dns_server_t dns_server;

int main(void) {
    stdio_init_all();
    if (!auth_self_test()) panic("SHA-256 self-test failed");
    if (cyw43_arch_init() != PICO_OK) panic("CYW43 init failed");
    ir_init();
    const btstack_tlv_t *tlv = btstack_tlv_flash_bank_init_instance(
        &bond_storage, pico_flash_bank_instance(), NULL);
    btstack_tlv_set_instance(tlv, &bond_storage);
    le_device_db_tlv_configure(tlv, &bond_storage);
    ir_store_init(tlv, &bond_storage);
    tv_target_init(tlv, &bond_storage);
    btstack_main(0, NULL);
    cyw43_arch_enable_sta_mode();
    printf("Connecting Wi-Fi while BLE advertises...\n");
    int result = -1;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        result = cyw43_arch_wifi_connect_timeout_ms(
            LOCAL_WIFI_SSID, LOCAL_WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000);
        if (result == 0) break;
        printf("Router connection attempt %u failed: %d\n", attempt + 1, result);
        sleep_ms(1000);
    }
    if (result == 0) {
        cyw43_arch_lwip_begin();
        printf("Wi-Fi connected at %s\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));
        cyw43_arch_lwip_end();
        if (!http_server_start()) printf("HTTP server failed to start\n");
    } else {
        printf("Router connection failed: %d; starting access point\n", result);
        cyw43_arch_disable_sta_mode();
        cyw43_arch_enable_ap_mode(LOCAL_AP_SSID, LOCAL_AP_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);
        ip4_addr_t mask, gateway;
        gateway.addr = PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);
        mask.addr = PP_HTONL(CYW43_DEFAULT_IP_MASK);
        dhcp_server_init(&dhcp_server, &cyw43_state.netif[CYW43_ITF_AP], &gateway, &mask);
        dns_server_init(&dns_server, &cyw43_state.netif[CYW43_ITF_AP], &gateway);
        if (!http_server_start()) printf("HTTP server failed to start\n");
        printf("Access point %s at %s\n", LOCAL_AP_SSID, ip4addr_ntoa(&gateway));
    }
    btstack_run_loop_execute();
    return 0;
}
