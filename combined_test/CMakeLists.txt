cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(tv_remote_wifi_test C CXX ASM)
pico_sdk_init()

add_executable(tv_remote_wifi_test
    main.c
    http_remote.c
    auth.c
    ir.c
    ir_store.c
    ${CMAKE_CURRENT_LIST_DIR}/../pico-examples/pico_w/wifi/access_point/dhcpserver/dhcpserver.c
    ${CMAKE_CURRENT_LIST_DIR}/../pico-examples/pico_w/wifi/access_point/dnsserver/dnsserver.c
    ${CMAKE_CURRENT_LIST_DIR}/../bluetooth_test/tv_remote_test.c
)
target_include_directories(tv_remote_wifi_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../bluetooth_test
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../pico-examples/bluetooth/config
    ${CMAKE_CURRENT_LIST_DIR}/../pico-examples/pico_w/wifi
    ${CMAKE_CURRENT_LIST_DIR}/../pico-examples/pico_w/wifi/access_point/dhcpserver
    ${CMAKE_CURRENT_LIST_DIR}/../pico-examples/pico_w/wifi/access_point/dnsserver
)
target_link_libraries(tv_remote_wifi_test PRIVATE
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    pico_btstack_cyw43
    pico_btstack_ble
    pico_btstack_flash_bank
    pico_sha256
    pico_rand
    pico_unique_id
    hardware_pwm
)
pico_btstack_make_gatt_header(tv_remote_wifi_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../bluetooth_test/tv_remote_test.gatt)
pico_enable_stdio_usb(tv_remote_wifi_test 1)
pico_enable_stdio_uart(tv_remote_wifi_test 0)
pico_add_extra_outputs(tv_remote_wifi_test)
