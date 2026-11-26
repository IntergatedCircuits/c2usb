cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(c2usb_i2c_hid_target_loopback)

# the I2C HID device supports a single application, each one is tested in its own build
target_sources_ifdef(CONFIG_TEST_I2C_HID_APP_KEYBOARD app PRIVATE src/keyboard_tests.cpp)
target_sources_ifdef(CONFIG_TEST_I2C_HID_APP_RECORDING app PRIVATE src/app_tests.cpp)
