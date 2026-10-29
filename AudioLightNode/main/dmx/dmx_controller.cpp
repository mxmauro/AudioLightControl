#include "dmx_controller.h"
#include "soc/gpio_num.h"
#include "utils/config.h"
#include "utils/rgb.h"
#include <assert.h>
#include <cstdint>
#include <stdio.h>
#include <driver/gptimer.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_err.h>
#include <esp_intr_alloc.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <hal/uart_ll.h>
#include <hal/uart_periph.h>
#include <portmacro.h>
#include <storage/nvs.h>
#include <string.h>
#include <task.h>

static const char* TAG = "DMX-Controller";

#define DMX_DEFAULT_TX_GPIO_PIN    GPIO_NUM_NC
#define DMX_UART_PORT              UART_NUM_1
#define DMX_BAUD_RATE              250000
#define DMX_BREAK_US               184
#define DMX_MARK_AFTER_BREAK_US    24
#define DMX_FRAME_PERIOD_US        20000
#define DMX_FRAME_PERIOD_MS        20
#define DMX_TX_FRAME_SIZE          (DMX_MAX_CHANNELS + 1)
#define DMX_QUEUE_LENGTH           100
#define DMX_CMD_WAIT_TIMEOUT_MS    100
#define DMX_TX_DONE_TIMEOUT_MS     10
#define DMX_UART_TXFIFO_EMPTY_THR  16
#define DMX_TIMER_RESOLUTION_HZ    1000000

#define SET_HAS_WHITE_FLAG(fixture)      (fixture)->reserved[0] |= 0x01
#define SET_HAS_RGB_FLAG(fixture)        (fixture)->reserved[0] |= 0x02
#define SET_HAS_16BIT_PAN_FLAG(fixture)  (fixture)->reserved[0] |= 0x04
#define SET_HAS_16BIT_TILT_FLAG(fixture) (fixture)->reserved[0] |= 0x08

#define CHECK_HAS_WHITE_FLAG(fixture)      ((fixture)->reserved[0] & 0x01) != 0
#define CHECK_HAS_RGB_FLAG(fixture)        ((fixture)->reserved[0] & 0x02) != 0
#define CHECK_HAS_16BIT_PAN_FLAG(fixture)  ((fixture)->reserved[0] & 0x04) != 0
#define CHECK_HAS_16BIT_TILT_FLAG(fixture) ((fixture)->reserved[0] & 0x08) != 0

// -----------------------------------------------------------------------------

typedef enum OutputMode_e {
    OutputModeFixtureRgb = 0,
    OutputModeRawChannels,
} OutputMode_t;

typedef enum CommandType_e {
    CMD_SET = 0,
    CMD_FADE_TO,
    CMD_SET_AND_FADE_TO,
    CMD_SET_CHANNELS,
} CommandType_t;

typedef struct Command_s {
    CommandType_t type;
    RGBColor_t color;
    RGBColor_t target;
    uint32_t timeMs;
    uint16_t startChannel;
    size_t dataLen;
    uint8_t data[DMX_MAX_CHANNELS];
} Command_t;

typedef enum DmxTxPhase_e {
    DmxTxPhaseIdle = 0,
    DmxTxPhaseBreak,
    DmxTxPhaseMarkAfterBreak,
    DmxTxPhaseData,
} DmxTxPhase_t;

typedef struct Controller_s {
    Task_t task;
    QueueHandle_t queue;
    gpio_num_t txGpioPin;
    gptimer_handle_t frameTimer;
    intr_handle_t uartIntrHandle;
    uart_dev_t* uartHw;
    volatile size_t txIndex;
    volatile uint64_t nextFrameStartUs;
    volatile int pendingTxError;
    volatile bool txErrorPending;
    volatile DmxTxPhase_t txPhase;
    DmxFixtureConfig_t fixtures[DMX_MAX_FIXTURES];
    size_t fixtureCount;
} Controller_t;

// -----------------------------------------------------------------------------

static Controller_t sController = {};
static uint8_t sUniverse[DMX_TX_FRAME_SIZE];
static portMUX_TYPE sDmxSpinlock = portMUX_INITIALIZER_UNLOCKED;

// -----------------------------------------------------------------------------

static void dmxTask(Task_t task, void* arg);

static void applyColor(uint16_t r, uint16_t g, uint16_t b);
static void applyRawChannels(uint16_t startChannel, const uint8_t* data, size_t dataLen);
static void clearUniverse();
static void setFixtureColor(const DmxFixtureConfig_t* fixture, uint8_t red, uint8_t green, uint8_t blue);
static uint8_t color16ToDmx8(uint16_t value);
static void reportPendingTxError();

static esp_err_t startDmxTxEngine();
static void stopDmxTxEngine();
static esp_err_t sendDmxFrameBlocking();
static esp_err_t waitForTxIdle(TickType_t timeoutTicks);

static bool dmxTimerAlarmCallback(gptimer_handle_t timer, const gptimer_alarm_event_data_t* edata, void* user_data);
static void dmxUartIsrHandler(void* arg);
static void fillTxFifoFromIsr(Controller_t* controller);
static void setNextTimerAlarmFromIsr(Controller_t* controller, uint64_t alarmCount);
static void setPendingTxErrorFromIsr(Controller_t* controller, int err);
static void startBreakFromIsr(Controller_t* controller, uint64_t nowUs);
static void finishBreakFromIsr(Controller_t* controller, uint64_t nowUs);
static void startDataFromIsr(Controller_t* controller);
static void abortFrameFromIsr(Controller_t* controller, int err, uint64_t nowUs);
static void updateFixtureInternalFlags(DmxFixtureConfig_t* fixture);

static inline bool isDmxColoringValue(uint16_t value)
{
    return value >= DMX_COLORING_VALUE_MIN && value <= DMX_COLORING_VALUE_MAX;
}

// -----------------------------------------------------------------------------

esp_err_t dmxControllerReadConfig(binary_reader_t* br, DmxControllerConfig_t* config, GpioPinValidatorHandle_t pinValidator)
{
    uint16_t universeSize;
    uint8_t fixturesCount;
    uint8_t usedChannels[(DMX_MAX_CHANNELS + 7) / 8] = {};
    DmxFixtureConfig_t* fixture;
    const char* type;
    size_t typeLen;
    uint8_t channel;
    uint16_t channelIndex;
    uint16_t color;
    esp_err_t err;

    assert(br);
    assert(config);
    assert(pinValidator);

    memset(config, 0, sizeof(DmxControllerConfig_t));
    if ((!br_read_be16(br, &universeSize)) || universeSize != DMX_MAX_CHANNELS || (!br_ext_read_gpio(br, &config->txGpioPin)) ||
        (!br_read_byte(br, &fixturesCount)) || fixturesCount > DMX_MAX_FIXTURES)
    {
        err = ESP_ERR_INVALID_ARG;
        goto on_error;
    }

    err = gpioPinValidatorClaim(pinValidator, config->txGpioPin, GpioPinUseOutput);
    if (err != ESP_OK)
    {
    on_error:
        memset(config, 0, sizeof(DmxControllerConfig_t));
        return err;
    }
    config->fixturesCount = static_cast<size_t>(fixturesCount);

    for (size_t i = 0; i < fixturesCount; ++i)
    {
        fixture = &config->fixtures[i];

        if ((!br_read_str(br, &type, &typeLen)) || typeLen == 0 || typeLen > DMX_FIXTURE_MAX_TYPE_LEN ||
            (!br_read_be16(br, &fixture->startChannel)) || (!br_read_byte(br, &fixture->channelsCount)) || fixture->channelsCount == 0 ||
            fixture->channelsCount > DMX_FIXTURE_MAX_CHANNELS_COUNT || fixture->startChannel == 0 ||
            fixture->startChannel > DMX_MAX_CHANNELS - fixture->channelsCount + 1U)
        {
            err = ESP_ERR_INVALID_ARG;
            goto on_error;
        }
        memcpy(fixture->type, type, typeLen);
        fixture->type[typeLen] = 0;

        for (channel = 0; channel < fixture->channelsCount; ++channel)
        {
            channelIndex = fixture->startChannel + channel - 1U;

            if ((!br_read_be16(br, &color)) || (color > 0x00FFU && (!isDmxColoringValue(color))) ||
                (usedChannels[channelIndex >> 3] & (1U << (channelIndex & 7))) != 0)
            {
                err = ESP_ERR_INVALID_ARG;
                goto on_error;
            }
            usedChannels[channelIndex >> 3] |= 1U << (channelIndex & 7);
            fixture->coloring[channel] = color;
        }
    }

    return ESP_OK;
}

size_t dmxControllerCalcConfigSize(const DmxControllerConfig_t* config)
{
    size_t size;
    size_t i;

    assert(config);

    size = sizeof(uint16_t) + sizeof(uint8_t) + sizeof(uint8_t);
    for (i = 0; i < config->fixturesCount; ++i)
    {
        const DmxFixtureConfig_t* fixture = &config->fixtures[i];

        size += strlen(fixture->type) + 1;
        size += sizeof(uint16_t);
        size += sizeof(uint8_t);
        size += static_cast<size_t>(fixture->channelsCount) * sizeof(uint16_t);
    }

    return size;
}

bool dmxControllerWriteConfig(binary_writer_t* bw, const DmxControllerConfig_t* config)
{
    size_t i;

    assert(bw);
    assert(config);

    if ((!bw_write_be16(bw, DMX_MAX_CHANNELS)) || (!bw_ext_write_gpio(bw, config->txGpioPin)) ||
        (!bw_write_byte(bw, static_cast<uint8_t>(config->fixturesCount))))
    {
        return false;
    }

    for (i = 0; i < config->fixturesCount; ++i)
    {
        const DmxFixtureConfig_t* fixture = &config->fixtures[i];
        uint8_t channelIdx;

        if ((!bw_write_str(bw, fixture->type, -1)) || (!bw_write_be16(bw, fixture->startChannel)) ||
            (!bw_write_byte(bw, fixture->channelsCount)))
        {
            return false;
        }

        for (channelIdx = 0; channelIdx < fixture->channelsCount; channelIdx++)
        {
            if (!bw_write_be16(bw, fixture->coloring[channelIdx]))
            {
                return false;
            }
        }
    }

    return true;
}

esp_err_t dmxControllerInit(const DmxControllerConfig_t* config)
{
    uart_config_t uartConfig;
    gptimer_config_t timerConfig;
    gptimer_event_callbacks_t timerCallbacks;
    esp_err_t err;

    assert(config);

    err = gpioPinManagerClaim(config->txGpioPin, GpioPinUseOutput, "DMX TX");
    if (err != ESP_OK)
    {
        return err;
    }
    sController.txGpioPin = config->txGpioPin;

    memset(&uartConfig, 0, sizeof(uartConfig));
    uartConfig.baud_rate = DMX_BAUD_RATE;
    uartConfig.data_bits = UART_DATA_8_BITS;
    uartConfig.parity = UART_PARITY_DISABLE;
    uartConfig.stop_bits = UART_STOP_BITS_2;
    uartConfig.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uartConfig.rx_flow_ctrl_thresh = 122;
    uartConfig.source_clk = UART_SCLK_DEFAULT;

    err = uart_param_config(DMX_UART_PORT, &uartConfig);
    if (err != ESP_OK)
    {
        dmxControllerDeinit();
        return err;
    }

    err = uart_set_pin(DMX_UART_PORT, config->txGpioPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
    {
        dmxControllerDeinit();
        return err;
    }

    sController.uartHw = UART_LL_GET_HW(DMX_UART_PORT);
    sController.txPhase = DmxTxPhaseIdle;
    sController.txIndex = 0;
    sController.nextFrameStartUs = 1;
    sController.pendingTxError = ESP_OK;
    sController.txErrorPending = false;
    sController.fixtureCount = config->fixturesCount;
    if (config->fixturesCount > 0)
    {
        memcpy(sController.fixtures, config->fixtures, config->fixturesCount * sizeof(DmxFixtureConfig_t));
        for (size_t i = 0; i < sController.fixtureCount; ++i)
        {
            updateFixtureInternalFlags(&sController.fixtures[i]);
        }
    }

    clearUniverse();
    uart_ll_inverse_signal(sController.uartHw, 0);
    uart_ll_txfifo_rst(sController.uartHw);
    uart_ll_disable_intr_mask(sController.uartHw, UART_INTR_TXFIFO_EMPTY | UART_INTR_TX_DONE);
    uart_ll_clr_intsts_mask(sController.uartHw, UART_INTR_TXFIFO_EMPTY | UART_INTR_TX_DONE);
    uart_ll_set_txfifo_empty_thr(sController.uartHw, DMX_UART_TXFIFO_EMPTY_THR);

    sController.queue = xQueueCreate(DMX_QUEUE_LENGTH, sizeof(Command_t));
    if (!sController.queue)
    {
        dmxControllerDeinit();
        return ESP_ERR_NO_MEM;
    }

    memset(&timerConfig, 0, sizeof(timerConfig));
    timerConfig.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    timerConfig.direction = GPTIMER_COUNT_UP;
    timerConfig.resolution_hz = DMX_TIMER_RESOLUTION_HZ;
    timerConfig.intr_priority = 1;

    err = gptimer_new_timer(&timerConfig, &sController.frameTimer);
    if (err != ESP_OK)
    {
        dmxControllerDeinit();
        return err;
    }

    memset(&timerCallbacks, 0, sizeof(timerCallbacks));
    timerCallbacks.on_alarm = dmxTimerAlarmCallback;
    err = gptimer_register_event_callbacks(sController.frameTimer, &timerCallbacks, &sController);
    if (err != ESP_OK)
    {
        dmxControllerDeinit();
        return err;
    }

    err = gptimer_enable(sController.frameTimer);
    if (err != ESP_OK)
    {
        dmxControllerDeinit();
        return err;
    }

    err = esp_intr_alloc(uart_periph_signal[DMX_UART_PORT].irq, ESP_INTR_FLAG_IRAM, dmxUartIsrHandler, &sController,
                         &sController.uartIntrHandle);
    if (err != ESP_OK)
    {
        dmxControllerDeinit();
        return err;
    }

    err = startDmxTxEngine();
    if (err != ESP_OK)
    {
        dmxControllerDeinit();
        return err;
    }

    sController.task = taskCreate(dmxTask, "DmxController", 4096, nullptr, 4, tskNO_AFFINITY);
    if (!sController.task)
    {
        dmxControllerDeinit();
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "DMX controller successfully initialized on GPIO %d with %u fixture entries.", static_cast<int>(config->txGpioPin),
             static_cast<unsigned>(config->fixturesCount));
    return ESP_OK;
}

void dmxControllerDeinit()
{
    if (sController.task)
    {
        taskJoin(sController.task);
        sController.task = nullptr;
    }

    stopDmxTxEngine();

    if (sController.txGpioPin != GPIO_NUM_NC && sController.uartHw)
    {
        clearUniverse();
        sendDmxFrameBlocking();
        uart_ll_disable_intr_mask(sController.uartHw, UART_INTR_TXFIFO_EMPTY | UART_INTR_TX_DONE);
        uart_ll_clr_intsts_mask(sController.uartHw, UART_INTR_TXFIFO_EMPTY | UART_INTR_TX_DONE);
        uart_ll_inverse_signal(sController.uartHw, 0);
        uart_ll_txfifo_rst(sController.uartHw);
        sController.uartHw = nullptr;
    }

    if (sController.uartIntrHandle)
    {
        esp_intr_free(sController.uartIntrHandle);
        sController.uartIntrHandle = nullptr;
    }

    if (sController.frameTimer)
    {
        gptimer_disable(sController.frameTimer);
        gptimer_del_timer(sController.frameTimer);
        sController.frameTimer = nullptr;
    }

    if (sController.queue)
    {
        vQueueDelete(sController.queue);
        sController.queue = nullptr;
    }

    clearUniverse();
    sController.fixtureCount = 0;
    sController.txPhase = DmxTxPhaseIdle;
    sController.txIndex = 0;
    sController.nextFrameStartUs = 1;
    sController.pendingTxError = ESP_OK;
    sController.txErrorPending = false;
    gpioPinManagerRelease(sController.txGpioPin);
    sController.txGpioPin = GPIO_NUM_NC;
}

esp_err_t dmxControllerSet(uint16_t r, uint16_t g, uint16_t b)
{
    Command_t cmd;

    if (!sController.task)
    {
        return ESP_ERR_INVALID_STATE;
    }

    cmd.type = CMD_SET;
    cmd.color.r = r;
    cmd.color.g = g;
    cmd.color.b = b;
    return (xQueueSend(sController.queue, &cmd, pdMS_TO_TICKS(DMX_CMD_WAIT_TIMEOUT_MS)) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t dmxControllerFadeTo(uint16_t r, uint16_t g, uint16_t b, uint32_t ms)
{
    Command_t cmd;

    if (!sController.task)
    {
        return ESP_ERR_INVALID_STATE;
    }

    cmd.type = CMD_FADE_TO;
    cmd.target.r = r;
    cmd.target.g = g;
    cmd.target.b = b;
    cmd.timeMs = ms;
    return (xQueueSend(sController.queue, &cmd, pdMS_TO_TICKS(DMX_CMD_WAIT_TIMEOUT_MS)) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}
esp_err_t dmxControllerSetAndFadeTo(uint16_t rStart, uint16_t gStart, uint16_t bStart, uint16_t rEnd, uint16_t gEnd, uint16_t bEnd,
                                    uint32_t ms)
{
    Command_t cmd;

    if (!sController.task)
    {
        return ESP_ERR_INVALID_STATE;
    }

    cmd.type = CMD_SET_AND_FADE_TO;
    cmd.color.r = rStart;
    cmd.color.g = gStart;
    cmd.color.b = bStart;
    cmd.target.r = rEnd;
    cmd.target.g = gEnd;
    cmd.target.b = bEnd;
    cmd.timeMs = ms;
    return (xQueueSend(sController.queue, &cmd, pdMS_TO_TICKS(DMX_CMD_WAIT_TIMEOUT_MS)) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t dmxControllerSetChannels(uint16_t startChannel, const uint8_t* data, size_t dataLen)
{
    Command_t cmd;

    if (!sController.task)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (!data || dataLen == 0 || dataLen > DMX_MAX_CHANNELS)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (startChannel == 0 || startChannel > DMX_MAX_CHANNELS - dataLen + 1)
    {
        return ESP_ERR_INVALID_ARG;
    }

    cmd.type = CMD_SET_CHANNELS;
    cmd.startChannel = startChannel;
    cmd.dataLen = dataLen;
    memcpy(cmd.data, data, dataLen);
    return (xQueueSend(sController.queue, &cmd, pdMS_TO_TICKS(DMX_CMD_WAIT_TIMEOUT_MS)) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

// -----------------------------------------------------------------------------

static void dmxTask(Task_t task, void* arg)
{
    RGBColor_t current;
    RGBColor_t start;
    RGBColor_t target;
    OutputMode_t outputMode = OutputModeFixtureRgb;
    bool fadeActive = false;
    TickType_t fadeStartTick = 0;
    TickType_t lastWakeTick;
    uint32_t fadeTimeMs = 0;

    static_cast<void>(arg);

    current.r = current.g = current.b = 0;
    start = current;
    target = current;
    applyColor(current.r, current.g, current.b);
    lastWakeTick = xTaskGetTickCount();

    while (!taskShouldQuit(task))
    {
        Command_t cmd;

        while (xQueueReceive(sController.queue, &cmd, 0) == pdTRUE)
        {
            switch (cmd.type)
            {
                case CMD_SET:
                    outputMode = OutputModeFixtureRgb;
                    current = cmd.color;
                    start = current;
                    target = current;
                    fadeTimeMs = 0;
                    fadeActive = false;
                    break;

                case CMD_FADE_TO:
                    outputMode = OutputModeFixtureRgb;
                    fadeTimeMs = cmd.timeMs;
                    start = current;
                    if (fadeTimeMs == 0)
                    {
                        current = cmd.target;
                        target = current;
                        fadeActive = false;
                    }
                    else
                    {
                        target = cmd.target;
                        fadeStartTick = xTaskGetTickCount();
                        fadeActive = true;
                    }
                    break;

                case CMD_SET_AND_FADE_TO:
                    outputMode = OutputModeFixtureRgb;
                    fadeTimeMs = cmd.timeMs;
                    if (fadeTimeMs == 0)
                    {
                        current = cmd.target;
                        target = current;
                        fadeActive = false;
                    }
                    else
                    {
                        current = cmd.color;
                        start = current;
                        target = cmd.target;
                        fadeStartTick = xTaskGetTickCount();
                        fadeActive = true;
                    }
                    if (!fadeActive)
                    {
                        start = current;
                    }
                    break;

                case CMD_SET_CHANNELS:
                    outputMode = OutputModeRawChannels;
                    fadeActive = false;
                    applyRawChannels(cmd.startChannel, cmd.data, cmd.dataLen);
                    break;
            }
        }

        if (outputMode == OutputModeFixtureRgb)
        {
            if (fadeActive)
            {
                TickType_t now = xTaskGetTickCount();
                uint32_t elapsedMs = static_cast<uint32_t>(pdTICKS_TO_MS(now - fadeStartTick));

                if (elapsedMs >= fadeTimeMs)
                {
                    current = target;
                    fadeActive = false;
                }
                else
                {
                    current = rgbInterpolate(&start, &target, elapsedMs, fadeTimeMs);
                }
            }

            applyColor(current.r, current.g, current.b);
        }

        reportPendingTxError();
        vTaskDelayUntil(&lastWakeTick, pdMS_TO_TICKS(DMX_FRAME_PERIOD_MS));
    }
}

static void applyColor(uint16_t r, uint16_t g, uint16_t b)
{
    uint8_t red = color16ToDmx8(r);
    uint8_t green = color16ToDmx8(g);
    uint8_t blue = color16ToDmx8(b);
    size_t i;

    portENTER_CRITICAL(&sDmxSpinlock);
    clearUniverse();
    for (i = 0; i < sController.fixtureCount; ++i)
    {
        setFixtureColor(&sController.fixtures[i], red, green, blue);
    }
    portEXIT_CRITICAL(&sDmxSpinlock);
}

static void applyRawChannels(uint16_t startChannel, const uint8_t* data, size_t dataLen)
{
    portENTER_CRITICAL(&sDmxSpinlock);
    memcpy(&sUniverse[startChannel], data, dataLen);
    portEXIT_CRITICAL(&sDmxSpinlock);
}

static void clearUniverse()
{
    memset(sUniverse, 0, sizeof(sUniverse));
    sUniverse[0] = 0;
}

static void setFixtureColor(const DmxFixtureConfig_t* fixture, uint8_t red, uint8_t green, uint8_t blue)
{
    uint8_t* universe_pos;
    uint8_t channelIdx;
    uint8_t white = 0;

    assert(fixture);

    if (CHECK_HAS_WHITE_FLAG(fixture))
    {
        if (CHECK_HAS_RGB_FLAG(fixture))
        {
            white = red;
            if (green < white)
            {
                white = green;
            }
            if (blue < white)
            {
                white = blue;
            }
            red -= white;
            green -= white;
            blue -= white;
        }
        else
        {
            white = static_cast<uint8_t>((2126UL * static_cast<uint32_t>(red) + 7152UL * static_cast<uint32_t>(green) +
                722UL * static_cast<uint32_t>(blue) + 5000UL) / 10000UL);
        }
    }

    universe_pos = sUniverse + fixture->startChannel;
    for (channelIdx = 0; channelIdx < fixture->channelsCount; ++channelIdx)
    {
        uint16_t color = fixture->coloring[channelIdx];

        if (color < 0x0100U)
        {
            *universe_pos = static_cast<uint8_t>(color);
        }
        else
        {
            switch (color)
            {
                case DMX_COLORING_VALUE_DIMMER:
                case DMX_COLORING_VALUE_DIMMER_FINE:
                case DMX_COLORING_VALUE_PAN_FINE:
                case DMX_COLORING_VALUE_TILT_FINE:
                   *universe_pos = 0xFF;
                    break;

                case DMX_COLORING_VALUE_RED:
                case DMX_COLORING_VALUE_RED_16:
                    *universe_pos = red;
                    break;

                case DMX_COLORING_VALUE_GREEN:
                case DMX_COLORING_VALUE_GREEN_16:
                    *universe_pos = green;
                    break;

                case DMX_COLORING_VALUE_BLUE:
                case DMX_COLORING_VALUE_BLUE_16:
                    *universe_pos = blue;
                    break;

                case DMX_COLORING_VALUE_WHITE:
                case DMX_COLORING_VALUE_WHITE_16:
                    *universe_pos = white;
                    break;

                case DMX_COLORING_VALUE_UV:
                case DMX_COLORING_VALUE_UV_16:
                case DMX_COLORING_VALUE_STROBE:
                    *universe_pos = 0;
                    break;

                case DMX_COLORING_VALUE_PAN:
                case DMX_COLORING_VALUE_TILT:
                    *universe_pos = 0x7F;
                    break;

                default:
                    assert(false);
                    *universe_pos = 0;
                    break;
            }
        }

        universe_pos++;
    }
}

static uint8_t color16ToDmx8(uint16_t value)
{
    return static_cast<uint8_t>((static_cast<uint32_t>(value) + 128U) / 257U);
}

static void reportPendingTxError()
{
    int err;
    bool hasPending;

    portENTER_CRITICAL(&sDmxSpinlock);
    hasPending = sController.txErrorPending;
    err = sController.pendingTxError;
    sController.txErrorPending = false;
    portEXIT_CRITICAL(&sDmxSpinlock);

    if (hasPending)
    {
        ESP_LOGW(TAG, "Unable to transmit DMX frame. Error: %d.", err);
    }
}

static esp_err_t startDmxTxEngine()
{
    gptimer_alarm_config_t alarmConfig;
    esp_err_t err;

    memset(&alarmConfig, 0, sizeof(alarmConfig));

    if (!sController.frameTimer)
    {
        return ESP_ERR_INVALID_STATE;
    }

    alarmConfig.alarm_count = sController.nextFrameStartUs;
    alarmConfig.reload_count = 0;
    alarmConfig.flags.auto_reload_on_alarm = false;

    err = gptimer_set_raw_count(sController.frameTimer, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to reset DMX frame timer. Error: %d.", err);
        return err;
    }

    err = gptimer_set_alarm_action(sController.frameTimer, &alarmConfig);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to arm DMX frame timer. Error: %d.", err);
        return err;
    }

    err = gptimer_start(sController.frameTimer);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start DMX frame timer. Error: %d.", err);
        return err;
    }

    return ESP_OK;
}

static void stopDmxTxEngine()
{
    if (sController.frameTimer)
    {
        gptimer_stop(sController.frameTimer);
        gptimer_set_alarm_action(sController.frameTimer, nullptr);
    }
    if (sController.uartHw)
    {
        uart_ll_disable_intr_mask(sController.uartHw, UART_INTR_TXFIFO_EMPTY | UART_INTR_TX_DONE);
        uart_ll_clr_intsts_mask(sController.uartHw, UART_INTR_TXFIFO_EMPTY | UART_INTR_TX_DONE);
        uart_ll_inverse_signal(sController.uartHw, 0);
        uart_ll_txfifo_rst(sController.uartHw);
    }
    sController.txPhase = DmxTxPhaseIdle;
    sController.txIndex = 0;
}

static esp_err_t sendDmxFrameBlocking()
{
    size_t txIndex = 0;
    TickType_t deadlineTicks;

    if (!sController.uartHw)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (waitForTxIdle(pdMS_TO_TICKS(DMX_TX_DONE_TIMEOUT_MS)) != ESP_OK)
    {
        return ESP_ERR_TIMEOUT;
    }

    uart_ll_inverse_signal(sController.uartHw, UART_SIGNAL_TXD_INV);
    esp_rom_delay_us(DMX_BREAK_US);
    uart_ll_inverse_signal(sController.uartHw, 0);
    esp_rom_delay_us(DMX_MARK_AFTER_BREAK_US);

    while (txIndex < sizeof(sUniverse))
    {
        uint32_t txFifoSpace = uart_ll_get_txfifo_len(sController.uartHw);
        size_t remaining = sizeof(sUniverse) - txIndex;
        size_t toWrite = (remaining < txFifoSpace) ? remaining : txFifoSpace;

        if (toWrite == 0)
        {
            vTaskDelay(1);
            continue;
        }

        uart_ll_write_txfifo(sController.uartHw, &sUniverse[txIndex], static_cast<uint32_t>(toWrite));
        txIndex += toWrite;
    }

    deadlineTicks = xTaskGetTickCount() + pdMS_TO_TICKS(DMX_TX_DONE_TIMEOUT_MS);
    while (!uart_ll_is_tx_idle(sController.uartHw))
    {
        if (static_cast<int32_t>(deadlineTicks - xTaskGetTickCount()) <= 0)
        {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }

    return ESP_OK;
}

static esp_err_t waitForTxIdle(TickType_t timeoutTicks)
{
    TickType_t deadlineTicks = xTaskGetTickCount() + timeoutTicks;

    while (sController.uartHw && !uart_ll_is_tx_idle(sController.uartHw))
    {
        if (static_cast<int32_t>(deadlineTicks - xTaskGetTickCount()) <= 0)
        {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }

    return ESP_OK;
}

static bool IRAM_ATTR dmxTimerAlarmCallback(gptimer_handle_t timer, const gptimer_alarm_event_data_t* edata, void* user_data)
{
    Controller_t* controller = static_cast<Controller_t*>(user_data);

    static_cast<void>(timer);

    portENTER_CRITICAL_ISR(&sDmxSpinlock);
    switch (controller->txPhase)
    {
        case DmxTxPhaseIdle:
            startBreakFromIsr(controller, edata->count_value);
            break;

        case DmxTxPhaseBreak:
            finishBreakFromIsr(controller, edata->count_value);
            break;

        case DmxTxPhaseMarkAfterBreak:
            startDataFromIsr(controller);
            break;

        case DmxTxPhaseData:
            abortFrameFromIsr(controller, ESP_ERR_TIMEOUT, edata->count_value);
            break;
    }
    portEXIT_CRITICAL_ISR(&sDmxSpinlock);

    return false;
}

static void IRAM_ATTR dmxUartIsrHandler(void* arg)
{
    Controller_t* controller = static_cast<Controller_t*>(arg);
    uint32_t intrStatus = uart_ll_get_intsts_mask(controller->uartHw);

    if (intrStatus & UART_INTR_TXFIFO_EMPTY)
    {
        uart_ll_clr_intsts_mask(controller->uartHw, UART_INTR_TXFIFO_EMPTY);

        portENTER_CRITICAL_ISR(&sDmxSpinlock);
        if (controller->txPhase == DmxTxPhaseData)
        {
            fillTxFifoFromIsr(controller);
        }
        else
        {
            uart_ll_disable_intr_mask(controller->uartHw, UART_INTR_TXFIFO_EMPTY);
            setPendingTxErrorFromIsr(controller, ESP_FAIL);
        }
        portEXIT_CRITICAL_ISR(&sDmxSpinlock);
    }

    if (intrStatus & UART_INTR_TX_DONE)
    {
        uart_ll_clr_intsts_mask(controller->uartHw, UART_INTR_TX_DONE);

        portENTER_CRITICAL_ISR(&sDmxSpinlock);
        uart_ll_disable_intr_mask(controller->uartHw, UART_INTR_TX_DONE | UART_INTR_TXFIFO_EMPTY);
        if (controller->txPhase == DmxTxPhaseData && controller->txIndex >= sizeof(sUniverse))
        {
            controller->txPhase = DmxTxPhaseIdle;
            controller->txIndex = 0;
        }
        else
        {
            abortFrameFromIsr(controller, ESP_FAIL, controller->nextFrameStartUs);
        }
        portEXIT_CRITICAL_ISR(&sDmxSpinlock);
    }
}

static void IRAM_ATTR fillTxFifoFromIsr(Controller_t* controller)
{
    uint32_t fifoSpace;
    size_t remaining;
    size_t toWrite;

    fifoSpace = uart_ll_get_txfifo_len(controller->uartHw);
    remaining = sizeof(sUniverse) - controller->txIndex;
    toWrite = (remaining < fifoSpace) ? remaining : fifoSpace;

    if (toWrite > 0)
    {
        uart_ll_write_txfifo(controller->uartHw, &sUniverse[controller->txIndex], static_cast<uint32_t>(toWrite));
        controller->txIndex += toWrite;
    }

    if (controller->txIndex >= sizeof(sUniverse))
    {
        uart_ll_disable_intr_mask(controller->uartHw, UART_INTR_TXFIFO_EMPTY);
        uart_ll_clr_intsts_mask(controller->uartHw, UART_INTR_TX_DONE);
        uart_ll_ena_intr_mask(controller->uartHw, UART_INTR_TX_DONE);
    }
    else
    {
        uart_ll_clr_intsts_mask(controller->uartHw, UART_INTR_TXFIFO_EMPTY);
        uart_ll_ena_intr_mask(controller->uartHw, UART_INTR_TXFIFO_EMPTY);
    }
}

static void IRAM_ATTR setNextTimerAlarmFromIsr(Controller_t* controller, uint64_t alarmCount)
{
    gptimer_alarm_config_t alarmConfig;

    memset(&alarmConfig, 0, sizeof(alarmConfig));

    alarmConfig.alarm_count = alarmCount;
    alarmConfig.reload_count = 0;
    alarmConfig.flags.auto_reload_on_alarm = false;
    gptimer_set_alarm_action(controller->frameTimer, &alarmConfig);
}

static void IRAM_ATTR setPendingTxErrorFromIsr(Controller_t* controller, int err)
{
    controller->pendingTxError = err;
    controller->txErrorPending = true;
}

static void IRAM_ATTR startBreakFromIsr(Controller_t* controller, uint64_t nowUs)
{
    controller->txPhase = DmxTxPhaseBreak;
    controller->nextFrameStartUs = nowUs + DMX_FRAME_PERIOD_US;
    uart_ll_inverse_signal(controller->uartHw, UART_SIGNAL_TXD_INV);
    setNextTimerAlarmFromIsr(controller, nowUs + DMX_BREAK_US);
}

static void IRAM_ATTR finishBreakFromIsr(Controller_t* controller, uint64_t nowUs)
{
    controller->txPhase = DmxTxPhaseMarkAfterBreak;
    uart_ll_inverse_signal(controller->uartHw, 0);
    setNextTimerAlarmFromIsr(controller, nowUs + DMX_MARK_AFTER_BREAK_US);
}

static void IRAM_ATTR startDataFromIsr(Controller_t* controller)
{
    controller->txPhase = DmxTxPhaseData;
    controller->txIndex = 0;
    uart_ll_disable_intr_mask(controller->uartHw, UART_INTR_TXFIFO_EMPTY | UART_INTR_TX_DONE);
    uart_ll_clr_intsts_mask(controller->uartHw, UART_INTR_TXFIFO_EMPTY | UART_INTR_TX_DONE);
    uart_ll_txfifo_rst(controller->uartHw);
    fillTxFifoFromIsr(controller);
    setNextTimerAlarmFromIsr(controller, controller->nextFrameStartUs);
}

static void IRAM_ATTR abortFrameFromIsr(Controller_t* controller, int err, uint64_t nowUs)
{
    controller->txPhase = DmxTxPhaseIdle;
    controller->txIndex = 0;
    uart_ll_disable_intr_mask(controller->uartHw, UART_INTR_TXFIFO_EMPTY | UART_INTR_TX_DONE);
    uart_ll_clr_intsts_mask(controller->uartHw, UART_INTR_TXFIFO_EMPTY | UART_INTR_TX_DONE);
    uart_ll_inverse_signal(controller->uartHw, 0);
    uart_ll_txfifo_rst(controller->uartHw);
    setPendingTxErrorFromIsr(controller, err);

    if (controller->nextFrameStartUs <= nowUs)
    {
        controller->nextFrameStartUs = nowUs + DMX_FRAME_PERIOD_US;
    }
    setNextTimerAlarmFromIsr(controller, controller->nextFrameStartUs);
}

static void updateFixtureInternalFlags(DmxFixtureConfig_t* fixture)
{
    uint8_t channelIdx;

    assert(fixture);
    memset(fixture->reserved, 0, sizeof(fixture->reserved));

    for (channelIdx = 0; channelIdx < fixture->channelsCount; ++channelIdx)
    {
        switch (fixture->coloring[channelIdx])
        {
            case DMX_COLORING_VALUE_WHITE:
            case DMX_COLORING_VALUE_WHITE_16:
                SET_HAS_WHITE_FLAG(fixture);
                break;

            case DMX_COLORING_VALUE_RED:
            case DMX_COLORING_VALUE_RED_16:
            case DMX_COLORING_VALUE_GREEN:
            case DMX_COLORING_VALUE_GREEN_16:
            case DMX_COLORING_VALUE_BLUE:
            case DMX_COLORING_VALUE_BLUE_16:
                SET_HAS_RGB_FLAG(fixture);
                break;

            case DMX_COLORING_VALUE_PAN_FINE:
                SET_HAS_16BIT_PAN_FLAG(fixture);
                break;

            case DMX_COLORING_VALUE_TILT_FINE:
                SET_HAS_16BIT_TILT_FLAG(fixture);
                break;

        }
    }
}
