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
#include <string.h>
#include <task.h>

static const char* TAG = "5050-Controller";

#define PWM_FREQUENCY           4000
#define PWM_RESOLUTION_BITS     LEDC_TIMER_12_BIT
#define QUEUE_LENGTH            100
#define MAX_CMD_WAIT_TIMEOUT_MS 100
#define LED_UPDATE_PERIOD_MS    10

// -----------------------------------------------------------------------------

typedef enum CommandType_e {
    CMD_SET = 0,
    CMD_FADE_TO,
    CMD_SET_AND_FADE_TO,
} CommandType_t;

typedef struct Command_s {
    CommandType_t type;
    RGBColor_t color;
    RGBColor_t target;
    uint32_t timeMs;
} Command_t;

typedef struct Controller_s {
    Task_t task;
    QueueHandle_t queue;
} Controller_t;

// -----------------------------------------------------------------------------

static gpio_num_t redGpioPin = GPIO_NUM_NC;
static gpio_num_t greenGpioPin = GPIO_NUM_NC;
static gpio_num_t blueGpioPin = GPIO_NUM_NC;
static bool isActiveLow = false;
static Controller_t sController = {};

// -----------------------------------------------------------------------------

static void rgbLedTask(Task_t task, void* arg);
static void applyColor(uint16_t r, uint16_t g, uint16_t b);

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

esp_err_t ledStrip5050Init(const LedStrip5050Config_t* config)
{
    ledc_timer_config_t ledcTimer;
    ledc_channel_config_t ledcChannel;
    esp_err_t err;

    assert(config);

    if (sController.task || sController.queue)
    {
        return ESP_ERR_INVALID_STATE;
    }

    err = gpioPinManagerClaim(config->redGpioPin, GpioPinUseOutput, "RGB5050 red");
    if (err != ESP_OK)
    {
        return err;
    }
    err = gpioPinManagerClaim(config->greenGpioPin, GpioPinUseOutput, "RGB5050 green");
    if (err != ESP_OK)
    {
        gpioPinManagerRelease(config->redGpioPin);
        return err;
    }
    err = gpioPinManagerClaim(config->blueGpioPin, GpioPinUseOutput, "RGB5050 blue");
    if (err != ESP_OK)
    {
        gpioPinManagerRelease(config->greenGpioPin);
        gpioPinManagerRelease(config->redGpioPin);
        return err;
    }

    redGpioPin = config->redGpioPin;
    greenGpioPin = config->greenGpioPin;
    blueGpioPin = config->blueGpioPin;
    isActiveLow = config->isActiveLow;

    memset(&ledcTimer, 0, sizeof(ledcTimer));
    memset(&ledcChannel, 0, sizeof(ledcChannel));

    ledcTimer.speed_mode = LEDC_LOW_SPEED_MODE;
    ledcTimer.duty_resolution = PWM_RESOLUTION_BITS;
    ledcTimer.timer_num = LEDC_TIMER_0;
    ledcTimer.freq_hz = PWM_FREQUENCY;
    ledcTimer.clk_cfg = LEDC_AUTO_CLK;
    err = ledc_timer_config(&ledcTimer);
    if (err != ESP_OK)
    {
        ledStrip5050Deinit();
        return err;
    }

    ledcChannel.speed_mode = LEDC_LOW_SPEED_MODE;
    ledcChannel.timer_sel = LEDC_TIMER_0;
    ledcChannel.sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD;

    ledcChannel.gpio_num = config->redGpioPin;
    ledcChannel.channel = LEDC_CHANNEL_0;
    err = ledc_channel_config(&ledcChannel);
    if (err != ESP_OK)
    {
        ledStrip5050Deinit();
        return err;
    }

    ledcChannel.gpio_num = config->greenGpioPin;
    ledcChannel.channel = LEDC_CHANNEL_1;
    err = ledc_channel_config(&ledcChannel);
    if (err != ESP_OK)
    {
        ledStrip5050Deinit();
        return err;
    }

    ledcChannel.gpio_num = config->blueGpioPin;
    ledcChannel.channel = LEDC_CHANNEL_2;
    err = ledc_channel_config(&ledcChannel);
    if (err != ESP_OK)
    {
        ledStrip5050Deinit();
        return err;
    }

    sController.queue = xQueueCreate(QUEUE_LENGTH, sizeof(Command_t));
    if (!sController.queue)
    {
        ledStrip5050Deinit();
        return ESP_ERR_NO_MEM;
    }

    sController.task = taskCreate(rgbLedTask, "LedStrip5050", 3072, nullptr, 4, tskNO_AFFINITY);
    if (!sController.task)
    {
        ledStrip5050Deinit();
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "5050 led strip controller successfully initialized.");
    return ESP_OK;
}

void ledStrip5050Deinit()
{
    if (sController.task)
    {
        taskJoin(sController.task);
        sController.task = nullptr;
    }

    applyColor(0, 0, 0);

    if (sController.queue)
    {
        vQueueDelete(sController.queue);
        sController.queue = nullptr;
    }

    if (redGpioPin != GPIO_NUM_NC)
    {
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, isActiveLow ? 1 : 0);
        gpioPinManagerRelease(redGpioPin);
        redGpioPin = GPIO_NUM_NC;
    }
    if (greenGpioPin != GPIO_NUM_NC)
    {
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, isActiveLow ? 1 : 0);
        gpioPinManagerRelease(greenGpioPin);
        greenGpioPin = GPIO_NUM_NC;
    }
    if (blueGpioPin != GPIO_NUM_NC)
    {
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, isActiveLow ? 1 : 0);
        gpioPinManagerRelease(blueGpioPin);
        blueGpioPin = GPIO_NUM_NC;
    }
    isActiveLow = false;
}

esp_err_t ledStrip5050Set(uint16_t r, uint16_t g, uint16_t b)
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
    return (xQueueSend(sController.queue, &cmd, pdMS_TO_TICKS(MAX_CMD_WAIT_TIMEOUT_MS)) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t ledStrip5050FadeTo(uint16_t r, uint16_t g, uint16_t b, uint32_t ms)
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
    return (xQueueSend(sController.queue, &cmd, pdMS_TO_TICKS(MAX_CMD_WAIT_TIMEOUT_MS)) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t ledStrip5050SetAndFadeTo(uint16_t rStart, uint16_t gStart, uint16_t bStart, uint16_t rEnd, uint16_t gEnd, uint16_t bEnd,
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
    return (xQueueSend(sController.queue, &cmd, pdMS_TO_TICKS(MAX_CMD_WAIT_TIMEOUT_MS)) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

// -----------------------------------------------------------------------------

static void rgbLedTask(Task_t task, void*)
{
    RGBColor_t current;
    RGBColor_t start;
    RGBColor_t target;
    bool fadeActive = false;
    TickType_t fadeStartTick = 0;
    uint32_t fadeTimeMs = 0;

    current.r = current.g = current.b = 0xFFFFU;
    start = current;
    target = current;
    applyColor(current.r, current.g, current.b);

    while (!taskShouldQuit(task))
    {
        Command_t cmd;
        BaseType_t rc;

        rc = xQueueReceive(sController.queue, &cmd, pdMS_TO_TICKS(LED_UPDATE_PERIOD_MS));
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
                    applyColor(current.r, current.g, current.b);
                    break;

                case CMD_FADE_TO:
                    fadeTimeMs = cmd.timeMs;
                    start = current;
                    if (fadeTimeMs == 0)
                    {
                        current = cmd.target;
                        target = current;
                        fadeActive = false;
                        applyColor(current.r, current.g, current.b);
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
                    applyColor(current.r, current.g, current.b);
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
                current = rgbInterpolate(&start, &target, elapsedMs, fadeTimeMs);
            }

            applyColor(current.r, current.g, current.b);
        }
    }
}

static void applyColor(uint16_t r, uint16_t g, uint16_t b)
{
    r >>= 4;
    g >>= 4;
    b >>= 4;
    if (isActiveLow)
    {
        r = 4095 - r;
        g = 4095 - g;
        b = 4095 - b;
    }

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, r);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, g);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, b);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
}
