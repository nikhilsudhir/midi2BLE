idf_component_register(SRCS "oled_display.c" "battery.c" "mode_manager.c" "ble_midi.c" "usb_host_lib_main.c" "class_driver.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_adc esp_system bt nvs_flash espressif__usb
                    )
