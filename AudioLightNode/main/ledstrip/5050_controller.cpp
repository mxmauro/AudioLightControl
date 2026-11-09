#include "5050_controller.h"
#include "utils/config.h"
#include "utils/rgb.h"
#include <assert.h>
#include <driver/ledc.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <portmacro.h>
#include <stdlib.h>
#include <string.h>
#include <task.h>

static const char* TAG = "5050-Controller";

#define LED5050_MAX_CONTROLLERS        2U

#define PWM_FREQUENCY           4000
#define PWM_RESOLUTION_BITS     LEDC_TIMER_12_BIT
#define QUEUE_LENGTH            100
#define MAX_CMD_WAIT_TIMEOUT_MS 100
#define LED_UPDATE_PERIOD_MS    10

// -----------------------------------------------------------------------------
// Controller command and runtime types

typedef enum CommandType_e {
    CMD_SET = 0,
    CMD_FADE_TO,
    CMD_SET_AND_FADE_TO,
} CommandType_t;

typedef struct Command_s {
    CommandType_t type;
    RGBColor8_t color;
    RGBColor8_t target;
    uint32_t timeMs;
} Command_t;

typedef struct Controller_s {
    Task_t task;
    QueueHandle_t queue;
    gpio_num_t redGpioPin;
    gpio_num_t greenGpioPin;
    gpio_num_t blueGpioPin;
    bool isActiveLow;
    ledc_channel_t firstChannel;
} Controller_t;

// -----------------------------------------------------------------------------

static Controller_t* sControllers[LED5050_MAX_CONTROLLERS] = {};

// -----------------------------------------------------------------------------

static void rgbLedTask(Task_t task, void* arg);
static void applyColor(Controller_t* controller, uint8_t r, uint8_t g, uint8_t b);

// -----------------------------------------------------------------------------

esp_err_t ledStrip5050ReadConfig(binary_reader_t* br, LedStrip5050Config_t* config, GpioPinValidatorHandle_t pinValidator)
{
    uint8_t activeLow;
    esp_err_t err;

    assert(br);
    assert(config);
    assert(pinValidator);

    memset(config, 0, sizeof(LedStrip5050Config_t));
    if ((!br_ext_read_gpio(br, &config->redGpioPin)) || (!br_ext_read_gpio(br, &config->greenGpioPin)) ||
        (!br_ext_read_gpio(br, &config->blueGpioPin)))
    {
        err = ESP_ERR_INVALID_ARG;
        goto on_error;
    }
    err = gpioPinValidatorClaim(pinValidator, config->redGpioPin, GpioPinUseOutput);
    if (err != ESP_OK)
    {
    on_error:
        memset(config, 0, sizeof(LedStrip5050Config_t));
        return err;
    }
    err = gpioPinValidatorClaim(pinValidator, config->greenGpioPin, GpioPinUseOutput);
    if (err != ESP_OK)
    {
        goto on_error;
    }
    err = gpioPinValidatorClaim(pinValidator, config->blueGpioPin, GpioPinUseOutput);
    if (err != ESP_OK)
    {
        goto on_error;
    }

    if ((!br_read_byte(br, &activeLow)) || activeLow > 1)
    {
        err = ESP_ERR_INVALID_ARG;
        goto on_error;
    }
    config->isActiveLow = activeLow != 0;

    return ESP_OK;
}

size_t ledStrip5050CalcConfigSize(const LedStrip5050Config_t* config)
{
    assert(config);
    return sizeof(uint8_t) * 4;
}

bool ledStrip5050WriteConfig(binary_writer_t* bw, const LedStrip5050Config_t* config)
{
    assert(bw);
    assert(config);

    return bw_ext_write_gpio(bw, config->redGpioPin) && bw_ext_write_gpio(bw, config->greenGpioPin) &&
           bw_ext_write_gpio(bw, config->blueGpioPin) && bw_write_byte(bw, config->isActiveLow ? 1U : 0U);
}

esp_err_t ledStrip5050Init(size_t slot, const LedStrip5050Config_t* config)
{
    Controller_t* controller;
    ledc_timer_config_t ledcTimer;
    ledc_channel_config_t ledcChannel;
    esp_err_t err;

    assert(config);

    // Select one fixed PWM channel group for this output slot.
    if (slot >= LED5050_MAX_CONTROLLERS)
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
    controller->redGpioPin = GPIO_NUM_NC;
    controller->greenGpioPin = GPIO_NUM_NC;
    controller->blueGpioPin = GPIO_NUM_NC;

    // Reserve all output pins before configuring PWM hardware.
    err = gpioPinManagerClaim(config->redGpioPin, GpioPinUseOutput, "RGB5050 red");
    if (err != ESP_OK)
    {
        free(controller);
        return err;
    }
    err = gpioPinManagerClaim(config->greenGpioPin, GpioPinUseOutput, "RGB5050 green");
    if (err != ESP_OK)
    {
        gpioPinManagerRelease(config->redGpioPin);
        free(controller);
        return err;
    }
    err = gpioPinManagerClaim(config->blueGpioPin, GpioPinUseOutput, "RGB5050 blue");
    if (err != ESP_OK)
    {
        gpioPinManagerRelease(config->greenGpioPin);
        gpioPinManagerRelease(config->redGpioPin);
        free(controller);
        return err;
    }

    // Publish the slot state before failure cleanup can use it.
    sControllers[slot] = controller;
    controller->redGpioPin = config->redGpioPin;
    controller->greenGpioPin = config->greenGpioPin;
    controller->blueGpioPin = config->blueGpioPin;
    controller->isActiveLow = config->isActiveLow;
    controller->firstChannel = static_cast<ledc_channel_t>(slot * 3U);

    memset(&ledcTimer, 0, sizeof(ledcTimer));
    memset(&ledcChannel, 0, sizeof(ledcChannel));

    // Share timer zero while keeping PWM channels slot-specific.
    ledcTimer.speed_mode = LEDC_LOW_SPEED_MODE;
    ledcTimer.duty_resolution = PWM_RESOLUTION_BITS;
    ledcTimer.timer_num = LEDC_TIMER_0;
    ledcTimer.freq_hz = PWM_FREQUENCY;
    ledcTimer.clk_cfg = LEDC_AUTO_CLK;
    err = ledc_timer_config(&ledcTimer);
    if (err != ESP_OK)
    {
        ledStrip5050Deinit(slot);
        return err;
    }

    ledcChannel.speed_mode = LEDC_LOW_SPEED_MODE;
    ledcChannel.timer_sel = LEDC_TIMER_0;
    ledcChannel.sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD;

    ledcChannel.gpio_num = config->redGpioPin;
    ledcChannel.channel = controller->firstChannel;
    err = ledc_channel_config(&ledcChannel);
    if (err != ESP_OK)
    {
        ledStrip5050Deinit(slot);
        return err;
    }

    ledcChannel.gpio_num = config->greenGpioPin;
    ledcChannel.channel = static_cast<ledc_channel_t>(controller->firstChannel + 1);
    err = ledc_channel_config(&ledcChannel);
    if (err != ESP_OK)
    {
        ledStrip5050Deinit(slot);
        return err;
    }

    ledcChannel.gpio_num = config->blueGpioPin;
    ledcChannel.channel = static_cast<ledc_channel_t>(controller->firstChannel + 2);
    err = ledc_channel_config(&ledcChannel);
    if (err != ESP_OK)
    {
        ledStrip5050Deinit(slot);
        return err;
    }

    // Create the queue before starting the task that consumes it.
    controller->queue = xQueueCreate(QUEUE_LENGTH, sizeof(Command_t));
    if (!controller->queue)
    {
        ledStrip5050Deinit(slot);
        return ESP_ERR_NO_MEM;
    }

    controller->task = taskCreate(rgbLedTask, "LedStrip5050", 3072, controller, 4, tskNO_AFFINITY);
    if (!controller->task)
    {
        ledStrip5050Deinit(slot);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "5050 led strip controller successfully initialized.");
    return ESP_OK;
}

void ledStrip5050Deinit(size_t slot)
{
    Controller_t* controller;

    if (slot >= LED5050_MAX_CONTROLLERS)
    {
        return;
    }
    controller = sControllers[slot];
    if (!controller)
    {
        return;
    }

    // Stop the worker before deleting its queue or releasing PWM resources.
    if (controller->task)
    {
        taskJoin(controller->task);
        controller->task = nullptr;
    }

    applyColor(controller, 0, 0, 0);

    if (controller->queue)
    {
        vQueueDelete(controller->queue);
        controller->queue = nullptr;
    }

    // Return every pin and channel owned by this slot to its inactive state.
    if (controller->redGpioPin != GPIO_NUM_NC)
    {
        ledc_stop(LEDC_LOW_SPEED_MODE, controller->firstChannel, controller->isActiveLow ? 1 : 0);
        gpioPinManagerRelease(controller->redGpioPin);
        controller->redGpioPin = GPIO_NUM_NC;
    }
    if (controller->greenGpioPin != GPIO_NUM_NC)
    {
        ledc_stop(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(controller->firstChannel + 1), controller->isActiveLow ? 1 : 0);
        gpioPinManagerRelease(controller->greenGpioPin);
        controller->greenGpioPin = GPIO_NUM_NC;
    }
    if (controller->blueGpioPin != GPIO_NUM_NC)
    {
        ledc_stop(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(controller->firstChannel + 2), controller->isActiveLow ? 1 : 0);
        gpioPinManagerRelease(controller->blueGpioPin);
        controller->blueGpioPin = GPIO_NUM_NC;
    }
    controller->isActiveLow = false;
    sControllers[slot] = nullptr;
    free(controller);
}

esp_err_t ledStrip5050Set(size_t slot, uint16_t r, uint16_t g, uint16_t b)
{
    Controller_t* controller;
    Command_t cmd;

    if (slot >= LED5050_MAX_CONTROLLERS)
    {
        return ESP_ERR_INVALID_ARG;
    }
    controller = sControllers[slot];

    if (!controller || !controller->task)
    {
        return ESP_ERR_INVALID_STATE;
    }

    // The FreeRTOS queue serializes concurrent command producers.
    cmd.type = CMD_SET;
    cmd.color = rgbColorTo8(r, g, b);
    return (xQueueSend(controller->queue, &cmd, pdMS_TO_TICKS(MAX_CMD_WAIT_TIMEOUT_MS)) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t ledStrip5050FadeTo(size_t slot, uint16_t r, uint16_t g, uint16_t b, uint32_t ms)
{
    Controller_t* controller;
    Command_t cmd;

    if (slot >= LED5050_MAX_CONTROLLERS)
    {
        return ESP_ERR_INVALID_ARG;
    }
    controller = sControllers[slot];

    if (!controller || !controller->task)
    {
        return ESP_ERR_INVALID_STATE;
    }

    // The FreeRTOS queue serializes concurrent command producers.
    cmd.type = CMD_FADE_TO;
    cmd.target = rgbColorTo8(r, g, b);
    cmd.timeMs = ms;
    return (xQueueSend(controller->queue, &cmd, pdMS_TO_TICKS(MAX_CMD_WAIT_TIMEOUT_MS)) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t ledStrip5050SetAndFadeTo(size_t slot, uint16_t rStart, uint16_t gStart, uint16_t bStart, uint16_t rEnd, uint16_t gEnd,
                                    uint16_t bEnd, uint32_t ms)
{
    Controller_t* controller;
    Command_t cmd;

    if (slot >= LED5050_MAX_CONTROLLERS)
    {
        return ESP_ERR_INVALID_ARG;
    }
    controller = sControllers[slot];

    if (!controller || !controller->task)
    {
        return ESP_ERR_INVALID_STATE;
    }

    // The FreeRTOS queue serializes concurrent command producers.
    cmd.type = CMD_SET_AND_FADE_TO;
    cmd.color = rgbColorTo8(rStart, gStart, bStart);
    cmd.target = rgbColorTo8(rEnd, gEnd, bEnd);
    cmd.timeMs = ms;
    return (xQueueSend(controller->queue, &cmd, pdMS_TO_TICKS(MAX_CMD_WAIT_TIMEOUT_MS)) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

// -----------------------------------------------------------------------------
// Worker implementation

static void rgbLedTask(Task_t task, void* arg)
{
    Controller_t* controller = static_cast<Controller_t*>(arg);
    RGBColor8_t current;
    RGBColor8_t start;
    RGBColor8_t target;
    bool fadeActive = false;
    TickType_t fadeStartTick = 0;
    uint32_t fadeTimeMs = 0;

    taskSignalContinue(task);

    current.r = current.g = current.b = 0xFFU;
    start = current;
    target = current;
    applyColor(controller, current.r, current.g, current.b);

    // The worker exclusively owns animation state and PWM writes.
    while (!taskShouldQuit(task))
    {
        Command_t cmd;
        BaseType_t rc;

        rc = xQueueReceive(controller->queue, &cmd, pdMS_TO_TICKS(LED_UPDATE_PERIOD_MS));
        if (rc == pdTRUE)
        {
            switch (cmd.type)
            {
                case CMD_SET:
                    current = cmd.color;
                    start = current;
                    target = current;
                    fadeTimeMs = 0;
                    fadeActive = false;
                    applyColor(controller, current.r, current.g, current.b);
                    break;

                case CMD_FADE_TO:
                    fadeTimeMs = cmd.timeMs;
                    start = current;
                    if (fadeTimeMs == 0)
                    {
                        current = cmd.target;
                        target = current;
                        fadeActive = false;
                        applyColor(controller, current.r, current.g, current.b);
                    }
                    else
                    {
                        target = cmd.target;
                        fadeStartTick = xTaskGetTickCount();
                        fadeActive = true;
                    }
                    break;

                case CMD_SET_AND_FADE_TO:
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
                        target = cmd.target;
                        fadeStartTick = xTaskGetTickCount();
                        fadeActive = true;
                    }
                    start = current;
                    applyColor(controller, current.r, current.g, current.b);
                    break;
            }
        }
        else if (fadeActive)
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
                current = rgbInterpolate8(&start, &target, elapsedMs, fadeTimeMs);
            }

            applyColor(controller, current.r, current.g, current.b);
        }
    }
}

static void applyColor(Controller_t* controller, uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t redDuty = static_cast<uint16_t>((static_cast<uint32_t>(r) * 4095U + 127U) / 255U);
    uint16_t greenDuty = static_cast<uint16_t>((static_cast<uint32_t>(g) * 4095U + 127U) / 255U);
    uint16_t blueDuty = static_cast<uint16_t>((static_cast<uint32_t>(b) * 4095U + 127U) / 255U);

    if (controller->isActiveLow)
    {
        redDuty = 4095U - redDuty;
        greenDuty = 4095U - greenDuty;
        blueDuty = 4095U - blueDuty;
    }

    ledc_set_duty(LEDC_LOW_SPEED_MODE, controller->firstChannel, redDuty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, controller->firstChannel);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(controller->firstChannel + 1), greenDuty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(controller->firstChannel + 1));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(controller->firstChannel + 2), blueDuty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(controller->firstChannel + 2));
}
