#pragma once

#include "dmx/dmx_controller.h"
#include "ledstrip/5050_controller.h"
#include "ledstrip/ws281x_controller.h"
#include "utils/gpio_manager.h"
#include <esp_err.h>
#include <iot_comm/utils/binary.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Lighting configuration types

typedef enum LightingControllerType_e {
    LightingControllerTypeNone = 0,
    LightingControllerTypeDmx = 1,
    LightingControllerTypeRgb5050 = 2,
    LightingControllerTypeWs281x = 3,
} LightingControllerType_t;

typedef struct LightingControllerConfig_s {
    LightingControllerType_t type;
    union {
        DmxControllerConfig_t dmx;
        LedStrip5050Config_t rgb5050;
        LedStripWs281xConfig_t ws281x;
    } controller;
} LightingControllerConfig_t;

#define LIGHTING_CONTROLLER_MAX_LIGHTING 2
#define LIGHTING_CONTROLLER_TARGET_ALL   0xFFU

typedef struct LightingControllerConfigs_s {
    size_t lightingCount;
    LightingControllerConfig_t lighting[LIGHTING_CONTROLLER_MAX_LIGHTING];
} LightingControllerConfigs_t;

// -----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

esp_err_t lightingControllerReadConfig(binary_reader_t* br, LightingControllerConfig_t* config, GpioPinValidatorHandle_t pinValidator);
size_t lightingControllerCalcConfigSize(const LightingControllerConfig_t* config);
bool lightingControllerWriteConfig(binary_writer_t* bw, const LightingControllerConfig_t* config);

esp_err_t lightingControllerInit(const LightingControllerConfigs_t* configs);
void lightingControllerDeinit();

size_t lightingControllerGetCount();
LightingControllerType_t lightingControllerGetType(size_t slot);
bool lightingControllerIsValidTarget(uint8_t target);
const char* lightingControllerGetTypeName(LightingControllerType_t type);

esp_err_t lightingControllerSet(uint8_t target, uint16_t r, uint16_t g, uint16_t b);
esp_err_t lightingControllerFadeTo(uint8_t target, uint16_t r, uint16_t g, uint16_t b, uint32_t ms);
esp_err_t lightingControllerSetAndFadeTo(uint8_t target, uint16_t rStart, uint16_t gStart, uint16_t bStart, uint16_t rEnd,
                                         uint16_t gEnd, uint16_t bEnd, uint32_t ms);
esp_err_t lightingControllerSetDmxChannels(uint8_t target, uint16_t startChannel, const uint8_t* data, size_t dataLen);
esp_err_t lightingControllerSetWs281xPixels(uint8_t target, uint16_t firstLed, const uint8_t* data, uint16_t ledCount);

#ifdef __cplusplus
}
#endif // __cplusplus
