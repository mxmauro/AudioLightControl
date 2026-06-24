set(srcs "main.cpp"
         "ledstrip/5050_controller.cpp"
)
idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_ledc
)
