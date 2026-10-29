#pragma once

#include "dmx/dmx_controller.h"
#include "ledstrip/5050_controller.h"
#include "utils/gpio_manager.h"
#include <esp_err.h>
#include <iot_comm/utils/binary.h>
#include <stdint.h>

// -----------------------------------------------------------------------------

typedef enum OutputControllerType_e {
    OutputControllerTypeNone = 0,
    OutputControllerTypeDmx = 1,
    OutputControllerTypeRgb5050 = 2,
} OutputControllerType_t;

typedef struct OutputControllerConfig_s {
    OutputControllerType_t type;
    union
    {
        DmxControllerConfig_t dmx;
        LedStrip5050Config_t rgb5050;
    } controller;
} OutputControllerConfig_t;

// -----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

esp_err_t outputControllerReadConfig(binary_reader_t* br, OutputControllerConfig_t* config, GpioPinValidatorHandle_t pinValidator);
size_t outputControllerCalcConfigSize(const OutputControllerConfig_t* config);
bool outputControllerWriteConfig(binary_writer_t* bw, const OutputControllerConfig_t* config);

esp_err_t outputControllerInit(const OutputControllerConfig_t* config);
void outputControllerDeinit();

OutputControllerType_t outputControllerGetType();
bool outputControllerSupportsDmxChannelData();
const char* outputControllerGetTypeName(OutputControllerType_t type);

esp_err_t outputControllerSet(uint16_t r, uint16_t g, uint16_t b);
esp_err_t outputControllerFadeTo(uint16_t r, uint16_t g, uint16_t b, uint32_t ms);
esp_err_t outputControllerSetAndFadeTo(uint16_t rStart, uint16_t gStart, uint16_t bStart, uint16_t rEnd, uint16_t gEnd, uint16_t bEnd,
                                       uint32_t ms);
esp_err_t outputControllerSetDmxChannels(uint16_t startChannel, const uint8_t* data, size_t dataLen);

#ifdef __cplusplus
}
#endif // __cplusplus
