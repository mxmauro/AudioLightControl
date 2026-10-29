#pragma once

#include "utils/gpio_manager.h"
#include <driver/gpio.h>
#include <esp_err.h>
#include <iot_comm/utils/binary.h>
#include <stddef.h>
#include <stdint.h>

#define DMX_MAX_CHANNELS               128
#define DMX_MAX_FIXTURES               16
#define DMX_FIXTURE_MAX_TYPE_LEN       32
#define DMX_FIXTURE_MAX_CHANNELS_COUNT 32

#define DMX_COLORING_VALUE_DIMMER       0x0100U
#define DMX_COLORING_VALUE_DIMMER_FINE  0x0101U
#define DMX_COLORING_VALUE_RED          0x0102U
#define DMX_COLORING_VALUE_RED_16       0x0103U
#define DMX_COLORING_VALUE_GREEN        0x0104U
#define DMX_COLORING_VALUE_GREEN_16     0x0105U
#define DMX_COLORING_VALUE_BLUE         0x0106U
#define DMX_COLORING_VALUE_BLUE_16      0x0107U
#define DMX_COLORING_VALUE_WHITE        0x0108U
#define DMX_COLORING_VALUE_WHITE_16     0x0109U
#define DMX_COLORING_VALUE_UV           0x010AU
#define DMX_COLORING_VALUE_UV_16        0x010BU
#define DMX_COLORING_VALUE_PAN          0x010CU
#define DMX_COLORING_VALUE_PAN_FINE     0x010DU
#define DMX_COLORING_VALUE_TILT         0x010EU
#define DMX_COLORING_VALUE_TILT_FINE    0x010FU
#define DMX_COLORING_VALUE_STROBE       0x0110U

#define DMX_COLORING_VALUE_MIN DMX_COLORING_VALUE_DIMMER
#define DMX_COLORING_VALUE_MAX DMX_COLORING_VALUE_STROBE

#define DMX_COLORING_VALUE_DIMMER_COARSE DMX_COLORING_VALUE_DIMMER
#define DMX_COLORING_VALUE_PAN_COARSE    DMX_COLORING_VALUE_PAN
#define DMX_COLORING_VALUE_TILT_COARSE   DMX_COLORING_VALUE_TILT

typedef struct DmxFixtureConfig_s {
    char type[DMX_FIXTURE_MAX_TYPE_LEN + 1];
    uint16_t startChannel;
    uint8_t channelsCount;
    uint8_t reserved[3];
    uint16_t coloring[DMX_FIXTURE_MAX_CHANNELS_COUNT];
} DmxFixtureConfig_t;

typedef struct DmxControllerConfig_s {
    gpio_num_t txGpioPin;
    size_t fixturesCount;
    DmxFixtureConfig_t fixtures[DMX_MAX_FIXTURES];
} DmxControllerConfig_t;

// -----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

esp_err_t dmxControllerReadConfig(binary_reader_t* br, DmxControllerConfig_t* config, GpioPinValidatorHandle_t pinValidator);
size_t dmxControllerCalcConfigSize(const DmxControllerConfig_t* config);
bool dmxControllerWriteConfig(binary_writer_t* bw, const DmxControllerConfig_t* config);

esp_err_t dmxControllerInit(const DmxControllerConfig_t* config);
void dmxControllerDeinit();

esp_err_t dmxControllerSet(uint16_t r, uint16_t g, uint16_t b);
esp_err_t dmxControllerFadeTo(uint16_t r, uint16_t g, uint16_t b, uint32_t ms);
esp_err_t dmxControllerSetAndFadeTo(uint16_t rStart, uint16_t gStart, uint16_t bStart, uint16_t rEnd, uint16_t gEnd, uint16_t bEnd,
                                    uint32_t ms);
esp_err_t dmxControllerSetChannels(uint16_t startChannel, const uint8_t* data, size_t dataLen);

#ifdef __cplusplus
}
#endif // __cplusplus
