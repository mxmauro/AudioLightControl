# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.10)

set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(AudioLightNode5050)

# set(NVS_RO_CSV "${CMAKE_CURRENT_SOURCE_DIR}/partitions/nvs_ro/data.csv")
# nvs_create_partition_image(nvs_ro "${NVS_RO_CSV}" FLASH_IN_PROJECT)
