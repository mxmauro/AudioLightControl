#include "ir/receiver.h"
#include <driver/rmt_common.h>
#include <driver/rmt_rx.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>
#include <task.h>

static const char* TAG = "IR-RX";

#define IR_RX_RESOLUTION_HZ      1000000
#define IR_RX_MEM_BLOCK_SYMBOLS  64
#define IR_RX_TASK_STACK_SIZE    4096
#define IR_RX_TASK_PRIORITY      4
#define IR_RX_EVENT_QUEUE_LENGTH 4

#define IR_RX_SIGNAL_MIN_NS 1250
#define IR_RX_SIGNAL_MAX_NS 12000000

#define NEC_LEADING_CODE_DURATION_0 9000
#define NEC_LEADING_CODE_DURATION_1 4500
#define NEC_PAYLOAD_ZERO_DURATION_0 560
#define NEC_PAYLOAD_ZERO_DURATION_1 560
#define NEC_PAYLOAD_ONE_DURATION_0  560
#define NEC_PAYLOAD_ONE_DURATION_1  1690
#define NEC_REPEAT_CODE_DURATION_0  9000
#define NEC_REPEAT_CODE_DURATION_1  2250
#define NEC_TRAILING_PULSE_DURATION 560
#define NEC_FRAME_BIT_COUNT         32

#define NEC_SHORT_MARGIN_US     220
#define NEC_LEADING_MARGIN_0_US 2200
#define NEC_LEADING_MARGIN_1_US 1400
#define NEC_REPEAT_MARGIN_0_US  2200
#define NEC_REPEAT_MARGIN_1_US  700

#define IR_RX_MAX_PULSES (IR_RX_MEM_BLOCK_SYMBOLS * 2)

// -----------------------------------------------------------------------------

typedef struct IrRxEvent_s {
    size_t numSymbols;
    rmt_symbol_word_t symbols[IR_RX_MEM_BLOCK_SYMBOLS];
} IrRxEvent_t;

typedef struct IrReceiverContext_s {
    rmt_channel_handle_t channel{nullptr};
    QueueHandle_t eventQueue{nullptr};
    Task_t task{nullptr};
    gpio_num_t gpioPin{GPIO_NUM_NC};
    IrReceiverCodeHandler_t handler{nullptr};
    void* handlerCtx{nullptr};
    bool hasLastCode{false};
    uint32_t lastCode{0};
    rmt_symbol_word_t rawSymbols[IR_RX_MEM_BLOCK_SYMBOLS]{};
} IrReceiverContext_t;

typedef struct IrPulse_s {
    uint32_t level;
    uint32_t duration;
} IrPulse_t;

// -----------------------------------------------------------------------------

static IrReceiverContext_t sIrReceiver = {};

// -----------------------------------------------------------------------------

static void irReceiverTask(Task_t task, void* arg);

static bool onRmtReceiveDone(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t* edata, void* userData);

static bool checkInRange(uint32_t signalDuration, uint32_t specDuration, uint32_t margin);
static bool parseLogic0(const IrPulse_t* activePulse, const IrPulse_t* spacePulse, uint32_t activeLevel);
static bool parseLogic1(const IrPulse_t* activePulse, const IrPulse_t* spacePulse, uint32_t activeLevel);
static bool parseNecFrame(const rmt_symbol_word_t* symbols, size_t symbolCount, uint32_t* code);
static bool parseNecRepeatFrame(const rmt_symbol_word_t* symbols, size_t symbolCount);
static void logFramePreview(const rmt_symbol_word_t* symbols, size_t symbolCount);
static size_t normalizePulses(const rmt_symbol_word_t* symbols, size_t symbolCount, IrPulse_t* pulses, size_t maxPulseCount);
static void logPulsePreview(const IrPulse_t* pulses, size_t pulseCount);
static bool parseNecFrameFromPulses(const IrPulse_t* pulses, size_t pulseCount, uint32_t* code);
static bool parseNecRepeatFrameFromPulses(const IrPulse_t* pulses, size_t pulseCount);

// -----------------------------------------------------------------------------

esp_err_t irReceiverInit(gpio_num_t gpioPin, IrReceiverCodeHandler_t handler, void* ctx)
{
    rmt_rx_channel_config_t rxChannelConfig = {};
    rmt_rx_event_callbacks_t callbacks = {};
    esp_err_t err;

    if (!handler)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (sIrReceiver.channel || sIrReceiver.task || sIrReceiver.eventQueue)
    {
        return ESP_ERR_INVALID_STATE;
    }

    err = gpioPinManagerClaim(gpioPin, GpioPinUseInput, "IR receiver");
    if (err != ESP_OK)
    {
        return err;
    }

    sIrReceiver = {};
    sIrReceiver.gpioPin = gpioPin;
    sIrReceiver.handler = handler;
    sIrReceiver.handlerCtx = ctx;

    sIrReceiver.eventQueue = xQueueCreate(IR_RX_EVENT_QUEUE_LENGTH, sizeof(IrRxEvent_t));
    if (!sIrReceiver.eventQueue)
    {
        irReceiverDeinit();
        return ESP_ERR_NO_MEM;
    }

    rxChannelConfig.clk_src = RMT_CLK_SRC_DEFAULT;
    rxChannelConfig.resolution_hz = IR_RX_RESOLUTION_HZ;
    rxChannelConfig.mem_block_symbols = IR_RX_MEM_BLOCK_SYMBOLS;
    rxChannelConfig.gpio_num = gpioPin;
    err = rmt_new_rx_channel(&rxChannelConfig, &sIrReceiver.channel);
    if (err != ESP_OK)
    {
        irReceiverDeinit();
        return err;
    }

    callbacks.on_recv_done = onRmtReceiveDone;
    err = rmt_rx_register_event_callbacks(sIrReceiver.channel, &callbacks, sIrReceiver.eventQueue);
    if (err != ESP_OK)
    {
        irReceiverDeinit();
        return err;
    }

    err = rmt_enable(sIrReceiver.channel);
    if (err != ESP_OK)
    {
        irReceiverDeinit();
        return err;
    }

    sIrReceiver.task = taskCreate(irReceiverTask, "IrReceiver", IR_RX_TASK_STACK_SIZE, nullptr, IR_RX_TASK_PRIORITY, tskNO_AFFINITY);
    if (!sIrReceiver.task)
    {
        irReceiverDeinit();
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "IR receiver initialized on GPIO %d.", static_cast<int>(gpioPin));
    return ESP_OK;
}

void irReceiverDeinit()
{
    esp_err_t err;

    if (sIrReceiver.channel)
    {
        err = rmt_disable(sIrReceiver.channel);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        {
            ESP_LOGW(TAG, "Unable to disable IR receiver channel. Error: %d.", err);
        }
    }

    if (sIrReceiver.task)
    {
        taskJoin(sIrReceiver.task);
        sIrReceiver.task = nullptr;
    }

    if (sIrReceiver.channel)
    {
        err = rmt_del_channel(sIrReceiver.channel);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Unable to delete IR receiver channel. Error: %d.", err);
        }
        sIrReceiver.channel = nullptr;
    }

    if (sIrReceiver.eventQueue)
    {
        vQueueDelete(sIrReceiver.eventQueue);
        sIrReceiver.eventQueue = nullptr;
    }

    if (sIrReceiver.gpioPin != GPIO_NUM_NC)
    {
        gpioPinManagerRelease(sIrReceiver.gpioPin);
        sIrReceiver.gpioPin = GPIO_NUM_NC;
    }
    sIrReceiver.handler = nullptr;
    sIrReceiver.handlerCtx = nullptr;
    sIrReceiver.hasLastCode = false;
    sIrReceiver.lastCode = 0;
}

// -----------------------------------------------------------------------------

static void irReceiverTask(Task_t task, void* arg)
{
    static const rmt_receive_config_t receiveConfig = {
        .signal_range_min_ns = IR_RX_SIGNAL_MIN_NS,
        .signal_range_max_ns = IR_RX_SIGNAL_MAX_NS,
        .flags = {},
    };
    IrRxEvent_t event;
    uint32_t code;
    esp_err_t err;

    static_cast<void>(arg);

    err = rmt_receive(sIrReceiver.channel, sIrReceiver.rawSymbols, sizeof(sIrReceiver.rawSymbols), &receiveConfig);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Unable to start IR receive. Error: %d.", err);
    }
    else
    {
        ESP_LOGD(TAG, "IR receive armed on GPIO %d.", static_cast<int>(sIrReceiver.gpioPin));
    }

    while (!taskShouldQuit(task))
    {
        if (xQueueReceive(sIrReceiver.eventQueue, &event, pdMS_TO_TICKS(100)) != pdPASS)
        {
            continue;
        }

        ESP_LOGD(TAG, "RX done: %u symbols captured.", static_cast<unsigned>(event.numSymbols));
        logFramePreview(event.symbols, event.numSymbols);

        if (parseNecFrame(event.symbols, event.numSymbols, &code))
        {
            sIrReceiver.lastCode = code;
            sIrReceiver.hasLastCode = true;
            ESP_LOGD(TAG, "Decoded NEC frame: 0x%08" PRIX32 ".", code);
            sIrReceiver.handler(code, sIrReceiver.handlerCtx);
        }
        else if (parseNecRepeatFrame(event.symbols, event.numSymbols) && sIrReceiver.hasLastCode)
        {
            ESP_LOGD(TAG, "Decoded NEC repeat frame, reusing last code: 0x%08" PRIX32 ".", sIrReceiver.lastCode);
            sIrReceiver.handler(sIrReceiver.lastCode, sIrReceiver.handlerCtx);
        }
        else
        {
            ESP_LOGD(TAG, "Ignoring undecodable IR frame with %u symbols.", static_cast<unsigned>(event.numSymbols));
        }

        if (!taskShouldQuit(task))
        {
            err = rmt_receive(sIrReceiver.channel, sIrReceiver.rawSymbols, sizeof(sIrReceiver.rawSymbols), &receiveConfig);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Unable to restart IR receive. Error: %d.", err);
                break;
            }
        }
    }
}

static bool onRmtReceiveDone(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t* edata, void* userData)
{
    BaseType_t highTaskWakeup = pdFALSE;
    QueueHandle_t eventQueue = static_cast<QueueHandle_t>(userData);
    IrRxEvent_t event = {};

    static_cast<void>(channel);

    if (!edata || !eventQueue)
    {
        return false;
    }

    event.numSymbols = edata->num_symbols;
    if (event.numSymbols > IR_RX_MEM_BLOCK_SYMBOLS)
    {
        ESP_LOGW(TAG, "Received %u symbols, truncating to %u.", static_cast<unsigned>(event.numSymbols),
                 static_cast<unsigned>(IR_RX_MEM_BLOCK_SYMBOLS));
        event.numSymbols = IR_RX_MEM_BLOCK_SYMBOLS;
    }
    memcpy(event.symbols, edata->received_symbols, event.numSymbols * sizeof(rmt_symbol_word_t));
    xQueueSendFromISR(eventQueue, &event, &highTaskWakeup);
    return highTaskWakeup == pdTRUE;
}

static void logFramePreview(const rmt_symbol_word_t* symbols, size_t symbolCount)
{
    size_t previewCount;
    IrPulse_t pulses[IR_RX_MAX_PULSES] = {};
    size_t pulseCount;

    if ((!symbols) || (symbolCount == 0))
    {
        ESP_LOGD(TAG, "Frame preview: no symbols.");
        return;
    }

    previewCount = (symbolCount < 6) ? symbolCount : 6;
    for (size_t i = 0; i < previewCount; i++)
    {
        ESP_LOGD(TAG, "Symbol[%u] = {%d:%u},{%d:%u}.", static_cast<unsigned>(i), symbols[i].level0,
                 static_cast<unsigned>(symbols[i].duration0), symbols[i].level1, static_cast<unsigned>(symbols[i].duration1));
    }

    pulseCount = normalizePulses(symbols, symbolCount, pulses, IR_RX_MAX_PULSES);
    logPulsePreview(pulses, pulseCount);
}

static size_t normalizePulses(const rmt_symbol_word_t* symbols, size_t symbolCount, IrPulse_t* pulses, size_t maxPulseCount)
{
    auto appendPulse = [pulses, maxPulseCount](size_t* pulseCount, uint32_t level, uint32_t duration)
    {
        if (duration == 0)
        {
            return;
        }
        if ((*pulseCount > 0) && (pulses[*pulseCount - 1].level == level))
        {
            pulses[*pulseCount - 1].duration += duration;
            return;
        }
        if (*pulseCount >= maxPulseCount)
        {
            return;
        }
        pulses[*pulseCount].level = level;
        pulses[*pulseCount].duration = duration;
        (*pulseCount)++;
    };
    size_t pulseCount = 0;

    if ((!symbols) || (!pulses) || (maxPulseCount == 0))
    {
        return 0;
    }

    for (size_t i = 0; i < symbolCount; i++)
    {
        appendPulse(&pulseCount, symbols[i].level0, symbols[i].duration0);
        appendPulse(&pulseCount, symbols[i].level1, symbols[i].duration1);
    }

    return pulseCount;
}

static void logPulsePreview(const IrPulse_t* pulses, size_t pulseCount)
{
    size_t previewCount;

    if ((!pulses) || (pulseCount == 0))
    {
        ESP_LOGD(TAG, "Pulse preview: no pulses after normalization.");
        return;
    }

    ESP_LOGD(TAG, "Normalized pulse count: %u.", static_cast<unsigned>(pulseCount));
    previewCount = (pulseCount < 10) ? pulseCount : 10;
    for (size_t i = 0; i < previewCount; i++)
    {
        ESP_LOGD(TAG, "Pulse[%u] = {%u:%u}.", static_cast<unsigned>(i), static_cast<unsigned>(pulses[i].level),
                 static_cast<unsigned>(pulses[i].duration));
    }
}

static bool checkInRange(uint32_t signalDuration, uint32_t specDuration, uint32_t margin)
{
    return (signalDuration >= (specDuration - margin)) && (signalDuration <= (specDuration + margin));
}

static bool parseLogic0(const IrPulse_t* activePulse, const IrPulse_t* spacePulse, uint32_t activeLevel)
{
    if ((!activePulse) || (!spacePulse) || (activePulse->level != activeLevel) || (spacePulse->level == activeLevel))
    {
        return false;
    }

    return checkInRange(activePulse->duration, NEC_PAYLOAD_ZERO_DURATION_0, NEC_SHORT_MARGIN_US) &&
           checkInRange(spacePulse->duration, NEC_PAYLOAD_ZERO_DURATION_1, NEC_SHORT_MARGIN_US);
}

static bool parseLogic1(const IrPulse_t* activePulse, const IrPulse_t* spacePulse, uint32_t activeLevel)
{
    if ((!activePulse) || (!spacePulse) || (activePulse->level != activeLevel) || (spacePulse->level == activeLevel))
    {
        return false;
    }

    return checkInRange(activePulse->duration, NEC_PAYLOAD_ONE_DURATION_0, NEC_SHORT_MARGIN_US) &&
           checkInRange(spacePulse->duration, NEC_PAYLOAD_ONE_DURATION_1, NEC_SHORT_MARGIN_US);
}

static bool parseNecFrame(const rmt_symbol_word_t* symbols, size_t symbolCount, uint32_t* code)
{
    IrPulse_t pulses[IR_RX_MAX_PULSES] = {};
    size_t pulseCount = normalizePulses(symbols, symbolCount, pulses, IR_RX_MAX_PULSES);

    return parseNecFrameFromPulses(pulses, pulseCount, code);
}

static bool parseNecFrameFromPulses(const IrPulse_t* pulses, size_t pulseCount, uint32_t* code)
{
    const size_t minPulseCount = 2 + (NEC_FRAME_BIT_COUNT * 2);

    if ((!pulses) || (!code) || (pulseCount < minPulseCount))
    {
        return false;
    }

    for (size_t start = 0; (start + minPulseCount) <= pulseCount; start++)
    {
        uint32_t activeLevel = pulses[start].level;
        uint32_t decodedCode = 0;

        if (!checkInRange(pulses[start].duration, NEC_LEADING_CODE_DURATION_0, NEC_LEADING_MARGIN_0_US) ||
            !checkInRange(pulses[start + 1].duration, NEC_LEADING_CODE_DURATION_1, NEC_LEADING_MARGIN_1_US) ||
            (pulses[start + 1].level == activeLevel))
        {
            continue;
        }

        bool decoded = true;
        for (size_t bitIndex = 0; bitIndex < NEC_FRAME_BIT_COUNT; bitIndex++)
        {
            const IrPulse_t* activePulse = &pulses[start + 2 + (bitIndex * 2)];
            const IrPulse_t* spacePulse = &pulses[start + 3 + (bitIndex * 2)];

            if (parseLogic1(activePulse, spacePulse, activeLevel))
            {
                decodedCode |= static_cast<uint32_t>(1UL << bitIndex);
            }
            else if (!parseLogic0(activePulse, spacePulse, activeLevel))
            {
                decoded = false;
                break;
            }
        }

        if (!decoded)
        {
            continue;
        }

        if ((start + minPulseCount) < pulseCount)
        {
            const IrPulse_t* tailPulse = &pulses[start + minPulseCount];
            if ((tailPulse->level == activeLevel) && !checkInRange(tailPulse->duration, NEC_TRAILING_PULSE_DURATION, NEC_SHORT_MARGIN_US))
            {
                ESP_LOGD(TAG, "Decoded NEC frame but trailing pulse was unexpected: {%u:%u}.", static_cast<unsigned>(tailPulse->level),
                         static_cast<unsigned>(tailPulse->duration));
            }
        }

        *code = decodedCode;
        ESP_LOGD(TAG, "Decoded NEC frame from normalized pulses starting at pulse %u.", static_cast<unsigned>(start));
        return true;
    }

    return false;
}

static bool parseNecRepeatFrame(const rmt_symbol_word_t* symbols, size_t symbolCount)
{
    IrPulse_t pulses[IR_RX_MAX_PULSES] = {};
    size_t pulseCount = normalizePulses(symbols, symbolCount, pulses, IR_RX_MAX_PULSES);

    return parseNecRepeatFrameFromPulses(pulses, pulseCount);
}

static bool parseNecRepeatFrameFromPulses(const IrPulse_t* pulses, size_t pulseCount)
{
    if ((!pulses) || (pulseCount < 2))
    {
        return false;
    }

    for (size_t start = 0; (start + 2) <= pulseCount; start++)
    {
        uint32_t activeLevel = pulses[start].level;

        if (!checkInRange(pulses[start].duration, NEC_REPEAT_CODE_DURATION_0, NEC_REPEAT_MARGIN_0_US) ||
            !checkInRange(pulses[start + 1].duration, NEC_REPEAT_CODE_DURATION_1, NEC_REPEAT_MARGIN_1_US) ||
            (pulses[start + 1].level == activeLevel))
        {
            continue;
        }

        if ((start + 2) < pulseCount)
        {
            const IrPulse_t* tailPulse = &pulses[start + 2];
            if ((tailPulse->level == activeLevel) && !checkInRange(tailPulse->duration, NEC_TRAILING_PULSE_DURATION, NEC_SHORT_MARGIN_US))
            {
                ESP_LOGD(TAG, "Detected NEC repeat with non-standard trailing pulse: {%u:%u}.", static_cast<unsigned>(tailPulse->level),
                         static_cast<unsigned>(tailPulse->duration));
            }
        }

        ESP_LOGD(TAG, "Decoded NEC repeat frame from normalized pulses starting at pulse %u.", static_cast<unsigned>(start));
        return true;
    }

    return false;
}
