#include "5050_controller.h"
#include "esp_err.h"
#include "portmacro.h"
#include <driver/ledc.h>
#include <esp_check.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

static const char* TAG = "5050-Controller";

#define PWM_FREQUENCY 4000    // 4 kHz
#define PWM_RESOLUTION_BITS LEDC_TIMER_12_BIT // 0..4095

#define QUEUE_LENGTH 100

#define MAX_CMD_WAIT_TIMEOUT_MS 100

#define LED_UPDATE_PERIOD_MS 10 // Cannot be lower

// -----------------------------------------------------------------------------

typedef struct RgbColor_s {
    uint16_t r;
    uint16_t g;
    uint16_t b;
} RgbColor_t;

typedef enum CommandType_e {
    CMD_SET = 0,
    CMD_FADE_TO,
    CMD_SET_AND_FADE_TO,
    CMD_STOP,
} CommandType_t;

typedef struct Command_s {
    CommandType_t type;
    RgbColor_t    color;
    RgbColor_t    target;
    uint32_t      timeMs;
} Command_t;

typedef struct Controller_s {
    TaskHandle_t      task{ nullptr };
    QueueHandle_t     queue{ nullptr };
    TaskHandle_t      shutdownWaiterTask{ nullptr };
} Controller_t;

// -----------------------------------------------------------------------------

static gpio_num_t redGpioPin = GPIO_NUM_NC;
static gpio_num_t greenGpioPin = GPIO_NUM_NC;
static gpio_num_t blueGpioPin = GPIO_NUM_NC;

static bool isActiveLow = false;

static Controller_t sController = {};

// -----------------------------------------------------------------------------

static void rgb_led_task(void *arg);

static void apply_color(uint16_t r, uint16_t g, uint16_t b);
static uint16_t lerp_u16(uint16_t a, uint16_t b, uint32_t elapsed_ms, uint32_t totalMs);
static RgbColor_t rgb_interpolate(const RgbColor_t *start, const RgbColor_t *target, uint32_t elapsedMs, uint32_t totalMs);

// -----------------------------------------------------------------------------

esp_err_t ledStrip5050Init(LedStrip5050Config_t *config)
{
    ledc_timer_config_t ledcTimer;
    ledc_channel_config_t ledcChannel;
    esp_err_t err;

    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    // Set configuration of timer0 for high speed channels
    memset(&ledcTimer, 0, sizeof(ledcTimer));
    ledcTimer.speed_mode      = LEDC_LOW_SPEED_MODE; // timer mode
    ledcTimer.duty_resolution = PWM_RESOLUTION_BITS; // resolution of PWM duty
    ledcTimer.timer_num       = LEDC_TIMER_0;        // timer index
    ledcTimer.freq_hz         = PWM_FREQUENCY;       // frequency of PWM signal
    ledcTimer.clk_cfg         = LEDC_AUTO_CLK;       // Auto select the source clock
    ledcTimer.deconfigure     = false;
    err = ledc_timer_config(&ledcTimer);
    if (err != ESP_OK) {
        return err;
    }

    // Prepare and then apply the LEDC PWM channel configuration
    memset(&ledcChannel, 0, sizeof(ledcChannel));
    ledcChannel.gpio_num   = config->redGpioPin;
    ledcChannel.speed_mode = LEDC_LOW_SPEED_MODE;
    ledcChannel.channel    = LEDC_CHANNEL_0;
    ledcChannel.timer_sel  = LEDC_TIMER_0;
    ledcChannel.sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD;
    err = ledc_channel_config(&ledcChannel);
    if (err != ESP_OK) {
        return err;
    }

    ledcChannel.gpio_num = config->greenGpioPin;
    ledcChannel.channel = LEDC_CHANNEL_1;
    err = ledc_channel_config(&ledcChannel);
    if (err != ESP_OK) {
        return err;
    }

    ledcChannel.gpio_num = config->blueGpioPin;
    ledcChannel.channel = LEDC_CHANNEL_2;
    err = ledc_channel_config(&ledcChannel);
    if (err != ESP_OK) {
        return err;
    }

    redGpioPin = config->redGpioPin;
    greenGpioPin = config->greenGpioPin;
    blueGpioPin = config->blueGpioPin;
    isActiveLow = config->isActiveLow;

    // Initialize controller
    sController.queue = xQueueCreate(QUEUE_LENGTH, sizeof(Command_t));
    if (!sController.queue) {
        ledStrip5050Deinit();
        return ESP_ERR_NO_MEM;
    }

    // Create worker
    if (xTaskCreate(rgb_led_task, "LedStrip5050", 3072, nullptr, 4, &sController.task) != pdPASS) {
        ledStrip5050Deinit();
        return ESP_FAIL;
    }

    // Done
    ESP_LOGI(TAG, "5050 led strip controller sucessfully initialized.");
    return ESP_OK;
}

void ledStrip5050Deinit()
{
    if (sController.task) {
        Command_t cmd;

        sController.shutdownWaiterTask = xTaskGetCurrentTaskHandle();

        cmd.type = CMD_STOP;
        xQueueReset(sController.queue);
        xQueueSendToFront(sController.queue, &cmd, portMAX_DELAY);

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sController.task = nullptr;
    }

    apply_color(0, 0, 0);

    if (sController.queue) {
        vQueueDelete(sController.queue);
        sController.queue = nullptr;
    }

    if (redGpioPin != GPIO_NUM_NC) {
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, isActiveLow ? 1 : 0);
        redGpioPin = GPIO_NUM_NC;
    }
    if (greenGpioPin != GPIO_NUM_NC) {
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, isActiveLow ? 1 : 0);
        greenGpioPin = GPIO_NUM_NC;
    }
    if (blueGpioPin != GPIO_NUM_NC) {
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, isActiveLow ? 1 : 0);
        blueGpioPin = GPIO_NUM_NC;
    }
    isActiveLow = false;
}

esp_err_t ledStrip5050Set(uint16_t r, uint16_t g, uint16_t b)
{
    Command_t cmd;

    if (!sController.task) {
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

    if (!sController.task) {
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

    if (!sController.task) {
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

static void rgb_led_task(void *arg)
{
    RgbColor_t current, start, target;
    bool fadeActive = false;
    TickType_t fadeStartTick = 0;
    uint32_t fadeTimeMs = 0;

    current.r = current.g = current.b = 65535;
    start = current;
    target  = current;
    apply_color(current.r, current.g, current.b);

    while (true) {
        Command_t cmd;
        BaseType_t rc;

        rc = xQueueReceive(sController.queue, &cmd, fadeActive ? pdMS_TO_TICKS(LED_UPDATE_PERIOD_MS) : portMAX_DELAY);
        if (rc == pdTRUE) {
            switch (cmd.type) {
                case CMD_SET:
                    current = cmd.color;
                    start = current;
                    target = current;
                    fadeTimeMs = 0;
                    fadeActive = false;
                    apply_color(current.r, current.g, current.b);
                    break;

                case CMD_FADE_TO:
                    fadeTimeMs = cmd.timeMs;
                    start = current;
                    if (fadeTimeMs == 0) {
                        current = cmd.target;
                        target = current;
                        fadeActive = false;
                        apply_color(current.r, current.g, current.b);
                    }
                    else {
                        target = cmd.target;
                        fadeStartTick = xTaskGetTickCount();
                        fadeActive = true;
                    }
                    break;

                case CMD_SET_AND_FADE_TO:
                    fadeTimeMs = cmd.timeMs;
                    if (fadeTimeMs == 0) {
                        current = cmd.target;
                        target = current;
                        fadeActive = false;
                    }
                    else {
                        current = cmd.color;
                        target = cmd.target;
                        fadeStartTick = xTaskGetTickCount();
                        fadeActive = true;
                    }
                    start = current;
                    apply_color(current.r, current.g, current.b);
                    break;

                case CMD_STOP:
                    goto exit_task;
            }
        }
        else if (fadeActive) {
            TickType_t now = xTaskGetTickCount();
            uint32_t elapsedMs = (uint32_t)pdTICKS_TO_MS(now - fadeStartTick);

            if (elapsedMs >= fadeTimeMs) {
                current = target;
                fadeActive = false;
            }
            else
            {
                current = rgb_interpolate(&start, &target, elapsedMs, fadeTimeMs);
            }

            apply_color(current.r, current.g, current.b);
        }
    }

exit_task:
    if (sController.shutdownWaiterTask) {
        xTaskNotifyGive(sController.shutdownWaiterTask);
    }

    vTaskDelete(nullptr);
}

static void apply_color(uint16_t r, uint16_t g, uint16_t b)
{
    r >>= 4;
    g >>= 4;
    b >>= 4;
    if (isActiveLow) {
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

static uint16_t lerp_u16(uint16_t a, uint16_t b, uint32_t elapsedMs, uint32_t totalMs)
{
    int32_t value;

    if (elapsedMs >= totalMs) {
        return b;
    }
    value = (int32_t)a + (((int32_t)b - (int32_t)a) * (int32_t)elapsedMs) / (int32_t)totalMs;
    if (value < 0) {
        return 0;
    }
    if (value > 65535) {
        return 65535;
    }
    return (uint16_t)value;
}

static RgbColor_t rgb_interpolate(const RgbColor_t *start, const RgbColor_t *target, uint32_t elapsedMs, uint32_t totalMs)
{
    RgbColor_t out;

    out.r = lerp_u16(start->r, target->r, elapsedMs, totalMs);
    out.g = lerp_u16(start->g, target->g, elapsedMs, totalMs);
    out.b = lerp_u16(start->b, target->b, elapsedMs, totalMs);
    return out;
}
