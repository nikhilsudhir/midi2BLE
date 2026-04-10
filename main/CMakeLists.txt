idf_component_register(SRCS "ble_midi.c" "usb_host_lib_main.c" "class_driver.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_driver_gpio bt nvs_flash espressif__usb
                    )
