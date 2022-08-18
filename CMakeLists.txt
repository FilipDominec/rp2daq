cmake_minimum_required(VERSION 3.12)
set(CMAKE_CXX_STANDARD 14)
include(pico_sdk_import.cmake)
project(rp2daq)
pico_sdk_init()

# include_directories("include")
add_executable(rp2daq
        rp2daq.c
)


# file(GLOB SOURCES "include/*.c")

pico_enable_stdio_usb(rp2daq 1)
pico_enable_stdio_uart(rp2daq 0)

pico_add_extra_outputs(rp2daq)
target_link_libraries(rp2daq pico_stdlib hardware_pwm
        pico_unique_id hardware_watchdog hardware_adc hardware_i2c
        hardware_pio hardware_clocks hardware_spi pico_multicore
        hardware_dma hardware_irq )

#add_executable(pio_rp2daq)
