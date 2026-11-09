#include "ws281x_controller.h"
#include "utils/config.h"
#include "utils/rgb.h"
#include <soc/soc_caps.h>

#if !defined(SOC_RMT_SUPPORTED)
#error "WS281x output requires SOC_RMT_SUPPORTED"
#elif !SOC_RMT_SUPPORTED
#error "WS281x output requires RMT support on the selected target"
#endif

#include <hal/rmt_ll.h>

#if !defined(RMT_LL_TX_CANDIDATES_PER_INST)
#error "WS281x output requires RMT_LL_TX_CANDIDATES_PER_INST"
#endif

#include <assert.h>
#include <driver/rmt_common.h>
#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_rom_gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <soc/gpio_sig_map.h>
#include <stdlib.h>
#include <string.h>
#include <task.h>

static const char* TAG = "WS281x-Controller";

#define WS281X_MAX_CONTROLLERS        2U
#define WS281X_QUEUE_LENGTH           4U
#define WS281X_CMD_WAIT_TIMEOUT_MS    100U
#define WS281X_UPDATE_PERIOD_MS       10U
#define WS281X_RMT_RESOLUTION_HZ      10000000U
#define WS281X_TX_WAIT_TIMEOUT_MS     20

#if RMT_LL_TX_CANDIDATES_PER_INST < WS281X_MAX_CONTROLLERS
#error "WS281x output requires at least two RMT TX channels"
#endif

#if !defined(SOC_RMT_MEM_WORDS_PER_CHANNEL)
#error "WS281x output requires SOC_RMT_MEM_WORDS_PER_CHANNEL"
#endif

#if !defined(RMT_SIG_OUT0_IDX) || !defined(RMT_SIG_OUT1_IDX)
#error "WS281x output requires two RMT TX GPIO matrix signals"
#endif

// -----------------------------------------------------------------------------
// Controller command and runtime types

typedef enum CommandType_e {
    CMD_SET = 0,
    CMD_FADE_TO,
    CMD_SET_AND_FADE_TO,
    CMD_SET_PIXELS,
} CommandType_t;

typedef struct Command_s {
    CommandType_t type;
    RGBColor8_t   color;
    RGBColor8_t   target;
    uint16_t      firstLed;
    uint16_t      ledCount;
    uint8_t       data[WS281X_MAX_LEDS * 3U];
    uint32_t      timeMs;
} Command_t;

typedef struct Ws281xEncoder_s {
    rmt_encoder_t        base;
    rmt_encoder_handle_t bytesEncoder;
    rmt_encoder_handle_t resetEncoder;
    rmt_symbol_word_t    resetCode;
    int                  state;
} Ws281xEncoder_t;

typedef struct Controller_s {
    Task_t               task;
    QueueHandle_t        queue;
    rmt_channel_handle_t txChannel;
    rmt_encoder_handle_t encoder;
    gpio_num_t           dataGpioPin;
    gpio_num_t           mirrorGpioPin;
    uint16_t             ledCount;
    uint8_t              rmtSignalIndex;
    bool                 pinsClaimed;
    bool                 rmtSignalAllocated;
    RGBColor8_t          current[WS281X_MAX_LEDS];
    RGBColor8_t          start[WS281X_MAX_LEDS];
    uint8_t              grbData[WS281X_MAX_LEDS * 3U];
} Controller_t;

// -----------------------------------------------------------------------------

static Controller_t* sControllers[WS281X_MAX_CONTROLLERS] = {};
static uint8_t sRmtTxSignalMask = 0;
static const uint32_t sRmtOutputSignals[WS281X_MAX_CONTROLLERS] = {RMT_SIG_OUT0_IDX, RMT_SIG_OUT1_IDX};

// -----------------------------------------------------------------------------

static void ws281xTask(Task_t task, void* arg);
static esp_err_t newWs281xEncoder(rmt_encoder_handle_t* encoder);
static esp_err_t ws281xEncoderDel(rmt_encoder_t* encoder);
static esp_err_t ws281xEncoderReset(rmt_encoder_t* encoder);
static size_t ws281xEncoderEncode(rmt_encoder_t* encoder, rmt_channel_handle_t channel, const void* primaryData, size_t dataSize,
                                  rmt_encode_state_t* retState);
static void fillColor(Controller_t* controller, const RGBColor8_t* color);
static void applyFrame(Controller_t* controller);

// -----------------------------------------------------------------------------

esp_err_t ledStripWs281xReadConfig(binary_reader_t* br, LedStripWs281xConfig_t* config, GpioPinValidatorHandle_t pinValidator)
{
    esp_err_t err;

    assert(br);
    assert(config);
    assert(pinValidator);

    memset(config, 0, sizeof(LedStripWs281xConfig_t));
    config->dataGpioPin = GPIO_NUM_NC;
    config->mirrorGpioPin = GPIO_NUM_NC;
    if ((!br_ext_read_gpio(br, &config->dataGpioPin)) || (!br_ext_read_gpio(br, &config->mirrorGpioPin)) ||
        (!br_read_be16(br, &config->ledCount)) || config->ledCount == 0 || config->ledCount > WS281X_MAX_LEDS)
    {
        goto on_error;
    }

    err = gpioPinValidatorClaim(pinValidator, config->dataGpioPin, GpioPinUseOutput);
    if (err != ESP_OK)
    {
        goto on_error;
    }
    if (config->mirrorGpioPin != GPIO_NUM_NC)
    {
        err = gpioPinValidatorClaim(pinValidator, config->mirrorGpioPin, GpioPinUseOutput);
        if (err != ESP_OK)
        {
            goto on_error;
        }
    }
    return ESP_OK;

on_error:
    memset(config, 0, sizeof(LedStripWs281xConfig_t));
    config->dataGpioPin = GPIO_NUM_NC;
    config->mirrorGpioPin = GPIO_NUM_NC;
    return ESP_ERR_INVALID_ARG;
}

size_t ledStripWs281xCalcConfigSize(const LedStripWs281xConfig_t* config)
{
    assert(config);
    return sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint16_t);
}

bool ledStripWs281xWriteConfig(binary_writer_t* bw, const LedStripWs281xConfig_t* config)
{
    assert(bw);
    assert(config);

    return bw_ext_write_gpio(bw, config->dataGpioPin) && bw_ext_write_gpio(bw, config->mirrorGpioPin) &&
           bw_write_be16(bw, config->ledCount);
}

esp_err_t ledStripWs281xInit(size_t slot, const LedStripWs281xConfig_t* config)
{
    rmt_tx_channel_config_t txConfig = {};
    Controller_t* controller;
    esp_err_t err;

    assert(config);
    if (slot >= WS281X_MAX_CONTROLLERS || config->ledCount == 0 || config->ledCount > WS281X_MAX_LEDS ||
        sRmtTxSignalMask == ((1U << WS281X_MAX_CONTROLLERS) - 1U))
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (sControllers[slot])
    {
        return ESP_ERR_INVALID_STATE;
    }
    controller = static_cast<Controller_t*>(malloc(sizeof(Controller_t)));
    if (!controller)
    {
        return ESP_ERR_NO_MEM;
    }
    memset(controller, 0, sizeof(Controller_t));
    controller->dataGpioPin = GPIO_NUM_NC;
    controller->mirrorGpioPin = GPIO_NUM_NC;

    err = gpioPinManagerClaim(config->dataGpioPin, GpioPinUseOutput, "WS281x data");
    if (err != ESP_OK)
    {
        free(controller);
        return err;
    }
    if (config->mirrorGpioPin != GPIO_NUM_NC)
    {
        err = gpioPinManagerClaim(config->mirrorGpioPin, GpioPinUseOutput, "WS281x mirror");
        if (err != ESP_OK)
        {
            gpioPinManagerRelease(config->dataGpioPin);
            free(controller);
            return err;
        }
    }

    controller->dataGpioPin = config->dataGpioPin;
    controller->mirrorGpioPin = config->mirrorGpioPin;
    controller->ledCount = config->ledCount;
    controller->rmtSignalIndex = (sRmtTxSignalMask & 0x01U) == 0 ? 0 : 1;
    controller->pinsClaimed = true;
    sControllers[slot] = controller;

    txConfig.clk_src = RMT_CLK_SRC_DEFAULT;
    txConfig.resolution_hz = WS281X_RMT_RESOLUTION_HZ;
    txConfig.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
    txConfig.trans_queue_depth = 1;
    txConfig.gpio_num = config->dataGpioPin;
    err = rmt_new_tx_channel(&txConfig, &controller->txChannel);
    if (err != ESP_OK)
    {
        ledStripWs281xDeinit(slot);
        return err;
    }
    controller->rmtSignalAllocated = true;
    sRmtTxSignalMask |= static_cast<uint8_t>(1U << controller->rmtSignalIndex);
    err = newWs281xEncoder(&controller->encoder);
    if (err != ESP_OK)
    {
        ledStripWs281xDeinit(slot);
        return err;
    }
    err = rmt_enable(controller->txChannel);
    if (err != ESP_OK)
    {
        ledStripWs281xDeinit(slot);
        return err;
    }

    // Lighting starts before the IR receiver, so RMT TX allocation follows this controller order on ESP32-C3.
    if (controller->pinsClaimed && controller->mirrorGpioPin != GPIO_NUM_NC)
    {
        esp_rom_gpio_connect_out_signal(controller->mirrorGpioPin, sRmtOutputSignals[controller->rmtSignalIndex], false, false);
    }

    controller->queue = xQueueCreate(WS281X_QUEUE_LENGTH, sizeof(Command_t));
    if (!controller->queue)
    {
        ledStripWs281xDeinit(slot);
        return ESP_ERR_NO_MEM;
    }
    controller->task = taskCreate(ws281xTask, "Ws281xController", 6144, controller, 4, tskNO_AFFINITY);
    if (!controller->task)
    {
        ledStripWs281xDeinit(slot);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "WS281x controller initialized on GPIO %d%s with %u LEDs.", static_cast<int>(config->dataGpioPin),
             config->mirrorGpioPin == GPIO_NUM_NC ? "" : " and mirrored", static_cast<unsigned>(config->ledCount));
    return ESP_OK;
}

void ledStripWs281xDeinit(size_t slot)
{
    Controller_t* controller;

    if (slot >= WS281X_MAX_CONTROLLERS)
    {
        return;
    }
    controller = sControllers[slot];
    if (!controller)
    {
        return;
    }
    if (controller->task)
    {
        taskJoin(controller->task);
        controller->task = nullptr;
    }

    if (controller->txChannel && controller->encoder)
    {
        memset(controller->current, 0, sizeof(controller->current));
        applyFrame(controller);
    }
    if (controller->queue)
    {
        vQueueDelete(controller->queue);
        controller->queue = nullptr;
    }
    if (controller->txChannel)
    {
        (void)rmt_disable(controller->txChannel);
    }
    if (controller->encoder)
    {
        (void)rmt_del_encoder(controller->encoder);
        controller->encoder = nullptr;
    }
    if (controller->txChannel)
    {
        (void)rmt_del_channel(controller->txChannel);
        controller->txChannel = nullptr;
    }

    if (controller->mirrorGpioPin != GPIO_NUM_NC)
    {
        gpio_reset_pin(controller->mirrorGpioPin);
        gpioPinManagerRelease(controller->mirrorGpioPin);
    }
    if (controller->pinsClaimed && controller->dataGpioPin != GPIO_NUM_NC)
    {
        gpioPinManagerRelease(controller->dataGpioPin);
    }
    if (controller->rmtSignalAllocated)
    {
        sRmtTxSignalMask &= static_cast<uint8_t>(~(1U << controller->rmtSignalIndex));
    }
    memset(controller, 0, sizeof(Controller_t));
    controller->dataGpioPin = GPIO_NUM_NC;
    controller->mirrorGpioPin = GPIO_NUM_NC;
    sControllers[slot] = nullptr;
    free(controller);
}

esp_err_t ledStripWs281xSet(size_t slot, uint16_t r, uint16_t g, uint16_t b)
{
    Command_t cmd = {};

    Controller_t* controller;

    if (slot >= WS281X_MAX_CONTROLLERS || !(controller = sControllers[slot]) || !controller->task)
    {
        return ESP_ERR_INVALID_STATE;
    }
    cmd.type = CMD_SET;
    cmd.color = rgbColorTo8(r, g, b);
    return (xQueueSend(controller->queue, &cmd, pdMS_TO_TICKS(WS281X_CMD_WAIT_TIMEOUT_MS)) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t ledStripWs281xFadeTo(size_t slot, uint16_t r, uint16_t g, uint16_t b, uint32_t ms)
{
    Command_t cmd = {};

    Controller_t* controller;

    if (slot >= WS281X_MAX_CONTROLLERS || !(controller = sControllers[slot]) || !controller->task)
    {
        return ESP_ERR_INVALID_STATE;
    }
    cmd.type = CMD_FADE_TO;
    cmd.target = rgbColorTo8(r, g, b);
    cmd.timeMs = ms;
    return (xQueueSend(controller->queue, &cmd, pdMS_TO_TICKS(WS281X_CMD_WAIT_TIMEOUT_MS)) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t ledStripWs281xSetAndFadeTo(size_t slot, uint16_t rStart, uint16_t gStart, uint16_t bStart, uint16_t rEnd, uint16_t gEnd,
                                      uint16_t bEnd, uint32_t ms)
{
    Command_t cmd = {};

    Controller_t* controller;

    if (slot >= WS281X_MAX_CONTROLLERS || !(controller = sControllers[slot]) || !controller->task)
    {
        return ESP_ERR_INVALID_STATE;
    }
    cmd.type = CMD_SET_AND_FADE_TO;
    cmd.color = rgbColorTo8(rStart, gStart, bStart);
    cmd.target = rgbColorTo8(rEnd, gEnd, bEnd);
    cmd.timeMs = ms;
    return (xQueueSend(controller->queue, &cmd, pdMS_TO_TICKS(WS281X_CMD_WAIT_TIMEOUT_MS)) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t ledStripWs281xSetPixels(size_t slot, uint16_t firstLed, const uint8_t* data, uint16_t ledCount)
{
    Command_t cmd = {};
    size_t dataLen;

    Controller_t* controller;

    if (slot >= WS281X_MAX_CONTROLLERS || !data || !(controller = sControllers[slot]) || !controller->task || firstLed == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (ledCount == 0 || firstLed > controller->ledCount)
    {
        return ESP_OK;
    }
    ledCount = (ledCount > controller->ledCount - firstLed + 1U) ? controller->ledCount - firstLed + 1U : ledCount;
    dataLen = static_cast<size_t>(ledCount) * 3U;
    cmd.type = CMD_SET_PIXELS;
    cmd.firstLed = firstLed;
    cmd.ledCount = ledCount;
    memcpy(cmd.data, data, dataLen);
    return (xQueueSend(controller->queue, &cmd, pdMS_TO_TICKS(WS281X_CMD_WAIT_TIMEOUT_MS)) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

// -----------------------------------------------------------------------------
// Worker implementation

static void ws281xTask(Task_t task, void* arg)
{
    Controller_t* controller = static_cast<Controller_t*>(arg);
    RGBColor8_t target = {};
    bool fadeActive = false;
    TickType_t fadeStartTick = 0;
    uint32_t fadeTimeMs = 0;

    taskSignalContinue(task);

    applyFrame(controller);
    while (!taskShouldQuit(task))
    {
        Command_t cmd;
        BaseType_t rc;

        rc = xQueueReceive(controller->queue, &cmd, pdMS_TO_TICKS(WS281X_UPDATE_PERIOD_MS));
        if (rc == pdTRUE)
        {
            switch (cmd.type)
            {
                case CMD_SET:
                    fillColor(controller, &cmd.color);
                    fadeActive = false;
                    applyFrame(controller);
                    break;

                case CMD_FADE_TO:
                    memcpy(controller->start, controller->current, sizeof(controller->start));
                    target = cmd.target;
                    fadeTimeMs = cmd.timeMs;
                    if (fadeTimeMs == 0)
                    {
                        fillColor(controller, &target);
                        fadeActive = false;
                        applyFrame(controller);
                    }
                    else
                    {
                        fadeStartTick = xTaskGetTickCount();
                        fadeActive = true;
                    }
                    break;

                case CMD_SET_AND_FADE_TO:
                    fillColor(controller, &cmd.color);
                    target = cmd.target;
                    fadeTimeMs = cmd.timeMs;
                    if (fadeTimeMs == 0)
                    {
                        fillColor(controller, &target);
                        fadeActive = false;
                    }
                    else
                    {
                        memcpy(controller->start, controller->current, sizeof(controller->start));
                        fadeStartTick = xTaskGetTickCount();
                        fadeActive = true;
                    }
                    applyFrame(controller);
                    break;

                case CMD_SET_PIXELS:
                    for (uint16_t i = 0; i < cmd.ledCount; ++i)
                    {
                        RGBColor8_t* pixel = &controller->current[cmd.firstLed - 1U + i];

                        pixel->r = cmd.data[static_cast<size_t>(i) * 3U];
                        pixel->g = cmd.data[static_cast<size_t>(i) * 3U + 1U];
                        pixel->b = cmd.data[static_cast<size_t>(i) * 3U + 2U];
                    }
                    fadeActive = false;
                    applyFrame(controller);
                    break;
            }
        }
        else if (fadeActive)
        {
            TickType_t now = xTaskGetTickCount();
            uint32_t elapsedMs = static_cast<uint32_t>(pdTICKS_TO_MS(now - fadeStartTick));

            if (elapsedMs >= fadeTimeMs)
            {
                fillColor(controller, &target);
                fadeActive = false;
            }
            else
            {
                for (uint16_t i = 0; i < controller->ledCount; ++i)
                {
                    controller->current[i] = rgbInterpolate8(&controller->start[i], &target, elapsedMs, fadeTimeMs);
                }
            }
            applyFrame(controller);
        }
    }
}

static esp_err_t newWs281xEncoder(rmt_encoder_handle_t* encoder)
{
    Ws281xEncoder_t* wsEncoder;
    rmt_bytes_encoder_config_t bytesConfig = {};
    rmt_copy_encoder_config_t resetConfig = {};
    esp_err_t err;

    assert(encoder);
    *encoder = nullptr;
    wsEncoder = static_cast<Ws281xEncoder_t*>(rmt_alloc_encoder_mem(sizeof(Ws281xEncoder_t)));
    if (!wsEncoder)
    {
        return ESP_ERR_NO_MEM;
    }
    memset(wsEncoder, 0, sizeof(Ws281xEncoder_t));
    wsEncoder->base.encode = ws281xEncoderEncode;
    wsEncoder->base.del = ws281xEncoderDel;
    wsEncoder->base.reset = ws281xEncoderReset;

    bytesConfig.bit0.level0 = 1;
    bytesConfig.bit0.duration0 = 3;
    bytesConfig.bit0.level1 = 0;
    bytesConfig.bit0.duration1 = 9;
    bytesConfig.bit1.level0 = 1;
    bytesConfig.bit1.duration0 = 9;
    bytesConfig.bit1.level1 = 0;
    bytesConfig.bit1.duration1 = 3;
    bytesConfig.flags.msb_first = 1;
    err = rmt_new_bytes_encoder(&bytesConfig, &wsEncoder->bytesEncoder);
    if (err != ESP_OK)
    {
        free(wsEncoder);
        return err;
    }
    err = rmt_new_copy_encoder(&resetConfig, &wsEncoder->resetEncoder);
    if (err != ESP_OK)
    {
        (void)rmt_del_encoder(wsEncoder->bytesEncoder);
        free(wsEncoder);
        return err;
    }
    wsEncoder->resetCode.level0 = 0;
    wsEncoder->resetCode.duration0 = 250;
    wsEncoder->resetCode.level1 = 0;
    wsEncoder->resetCode.duration1 = 250;
    *encoder = &wsEncoder->base;
    return ESP_OK;
}

static esp_err_t ws281xEncoderDel(rmt_encoder_t* encoder)
{
    Ws281xEncoder_t* wsEncoder = __containerof(encoder, Ws281xEncoder_t, base);

    if (wsEncoder->bytesEncoder)
    {
        (void)rmt_del_encoder(wsEncoder->bytesEncoder);
    }
    if (wsEncoder->resetEncoder)
    {
        (void)rmt_del_encoder(wsEncoder->resetEncoder);
    }
    free(wsEncoder);
    return ESP_OK;
}

RMT_ENCODER_FUNC_ATTR
static esp_err_t ws281xEncoderReset(rmt_encoder_t* encoder)
{
    Ws281xEncoder_t* wsEncoder = __containerof(encoder, Ws281xEncoder_t, base);

    (void)rmt_encoder_reset(wsEncoder->bytesEncoder);
    (void)rmt_encoder_reset(wsEncoder->resetEncoder);
    wsEncoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

RMT_ENCODER_FUNC_ATTR
static size_t ws281xEncoderEncode(rmt_encoder_t* encoder, rmt_channel_handle_t channel, const void* primaryData, size_t dataSize,
                                   rmt_encode_state_t* retState)
{
    Ws281xEncoder_t* wsEncoder = __containerof(encoder, Ws281xEncoder_t, base);
    rmt_encode_state_t sessionState = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encodedSymbols = 0;

    switch (wsEncoder->state)
    {
        case 0:
            encodedSymbols += wsEncoder->bytesEncoder->encode(wsEncoder->bytesEncoder, channel, primaryData, dataSize, &sessionState);
            if (sessionState & RMT_ENCODING_COMPLETE)
            {
                wsEncoder->state = 1;
            }
            if (sessionState & RMT_ENCODING_MEM_FULL)
            {
                state = static_cast<rmt_encode_state_t>(state | RMT_ENCODING_MEM_FULL);
                goto on_done;
            }
            [[fallthrough]];

        case 1:
            encodedSymbols += wsEncoder->resetEncoder->encode(wsEncoder->resetEncoder, channel, &wsEncoder->resetCode,
                                                              sizeof(wsEncoder->resetCode), &sessionState);
            if (sessionState & RMT_ENCODING_COMPLETE)
            {
                state = static_cast<rmt_encode_state_t>(state | RMT_ENCODING_COMPLETE);
                wsEncoder->state = RMT_ENCODING_RESET;
            }
            if (sessionState & RMT_ENCODING_MEM_FULL)
            {
                state = static_cast<rmt_encode_state_t>(state | RMT_ENCODING_MEM_FULL);
            }
            break;

        default:
            wsEncoder->state = RMT_ENCODING_RESET;
            state = static_cast<rmt_encode_state_t>(state | RMT_ENCODING_COMPLETE);
            break;
    }

on_done:
    *retState = state;
    return encodedSymbols;
}

static void fillColor(Controller_t* controller, const RGBColor8_t* color)
{
    for (uint16_t i = 0; i < controller->ledCount; ++i)
    {
        controller->current[i] = *color;
    }
}

static void applyFrame(Controller_t* controller)
{
    rmt_transmit_config_t txConfig = {};
    size_t dataLen = static_cast<size_t>(controller->ledCount) * 3U;
    esp_err_t err;

    for (uint16_t i = 0; i < controller->ledCount; ++i)
    {
        controller->grbData[static_cast<size_t>(i) * 3U] = controller->current[i].g;
        controller->grbData[static_cast<size_t>(i) * 3U + 1U] = controller->current[i].r;
        controller->grbData[static_cast<size_t>(i) * 3U + 2U] = controller->current[i].b;
    }
    err = rmt_transmit(controller->txChannel, controller->encoder, controller->grbData, dataLen, &txConfig);
    if (err == ESP_OK)
    {
        err = rmt_tx_wait_all_done(controller->txChannel, WS281X_TX_WAIT_TIMEOUT_MS);
    }
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Unable to transmit WS281x frame. Error: %d.", err);
    }
}

