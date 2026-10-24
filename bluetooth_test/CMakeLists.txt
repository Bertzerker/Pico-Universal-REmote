cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(tv_remote_test C CXX ASM)
pico_sdk_init()

add_executable(tv_remote_test
    ${CMAKE_CURRENT_LIST_DIR}/../pico-examples/bluetooth/btstack_examples/main.c
    tv_remote_test.c
)
target_include_directories(tv_remote_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../pico-examples/bluetooth/config
)
target_link_libraries(tv_remote_test PRIVATE
    pico_stdlib
    pico_cyw43_arch_none
    pico_btstack_cyw43
    pico_btstack_ble
)
pico_btstack_make_gatt_header(tv_remote_test PRIVATE tv_remote_test.gatt)
pico_enable_stdio_usb(tv_remote_test 1)
pico_enable_stdio_uart(tv_remote_test 0)
pico_add_extra_outputs(tv_remote_test)
