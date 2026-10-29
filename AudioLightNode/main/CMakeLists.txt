set(srcs
    "main.cpp"
    "dmx/dmx_controller.cpp"
    "ledstrip/5050_controller.cpp"
    "output/output_controller.cpp"
    "ir/receiver.cpp"
    "utils/config.cpp"
    "utils/gpio_manager.cpp"
    "utils/hsv.cpp"
    "utils/math.cpp"
    "utils/rgb.cpp"
)
idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_ledc esp_driver_rmt esp_driver_uart esp_driver_gptimer
)
