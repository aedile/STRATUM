/*
 * input.cpp - medal controls for Frogger (held upright, like Pac-Man)
 *
 * Dig Dug is a four-way game: you dig in whichever direction the stick is held. Only the
 * dominant axis is reported, so a diagonal tilt never produces two directions, which is what
 * the four-way gate on the cabinet did mechanically.
 *
 *   tilt left / right   -> dig left / right, the direction of gravity within the panel plane
 *   tilt away / toward  -> dig up / down, how far gravity leaves that plane
 *   BOOT button         -> the pump
 *   PWR short press     -> coin, then start half a second later; long press (1 s) -> power off
 *
 * Angles are measured against a neutral pose captured on the first IMU read and again on each
 * coin: held upright gravity lies in the plane of the panel, so the raw pitch is pinned near its
 * limit and cannot swing both ways.
 */
#include "input.h"
#include "qmi8658.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "INPUT";
#define PIN_BTN_BOOT GPIO_NUM_9
#define PIN_BTN_PWR  GPIO_NUM_18
#define PIN_BAT_EN   GPIO_NUM_15
#define IMU_PERIOD_US 16000

#define DIG_DEG        10.0f    /* tilt this far to move */
#define X_SIGN (-1.0f)          /* flip if left/right are reversed */
#define Y_SIGN (-1.0f)          /* flip if forward/back are reversed */

static bool imu_ok, pwr_was_down;
static int64_t pwr_down_since, imu_last_us, coin_seq_start, last_log;
static int coin_seq;                     /* 0 idle, 1 coin held, 2 gap, 3 start held */
static float neutral_lr, neutral_ud, dbg_roll, dbg_pitch;
static bool have_neutral;
static int8_t dir_x, dir_y;              /* -1 / 0 / +1 */

static void read_angles(float *lr, float *ud)
{
    int16_t ax, ay, az;
    qmi8658_read_accel(&ax, &ay, &az);
    float in_plane = sqrtf((float)ax * ax + (float)ay * ay);
    *lr = atan2f((float)ay, (float)ax) * 57.2958f;
    *ud = atan2f((float)az, in_plane) * 57.2958f;
}

static void capture_neutral(void)
{
    if (!imu_ok) return;
    read_angles(&neutral_lr, &neutral_ud);
    have_neutral = true;
    dir_x = dir_y = 0;
}

static inline float wrap_deg(float d)
{
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

void input_init(void)
{
    gpio_config_t bat = {}; bat.pin_bit_mask = 1ULL << PIN_BAT_EN; bat.mode = GPIO_MODE_OUTPUT; gpio_config(&bat);
    gpio_set_level(PIN_BAT_EN, 1);
    gpio_config_t io = {}; io.pin_bit_mask = (1ULL << PIN_BTN_BOOT) | (1ULL << PIN_BTN_PWR); io.mode = GPIO_MODE_INPUT; io.pull_up_en = GPIO_PULLUP_ENABLE; gpio_config(&io);
    i2c_config_t i2c = {}; i2c.mode = I2C_MODE_MASTER; i2c.sda_io_num = GPIO_NUM_8; i2c.scl_io_num = GPIO_NUM_7;
    i2c.sda_pullup_en = GPIO_PULLUP_ENABLE; i2c.scl_pullup_en = GPIO_PULLUP_ENABLE; i2c.master.clk_speed = 100000;
    i2c_param_config(I2C_NUM_0, &i2c);
    esp_err_t err = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "I2C init failed: %s", esp_err_to_name(err));
    imu_ok = qmi8658_init();
    ESP_LOGI(TAG, "input ready (IMU %s); neutral pose is captured on the first read and on each coin", imu_ok ? "ok" : "missing");
}

void input_update(dd_input_t *in)
{
    int64_t now = esp_timer_get_time();
    bool boot = gpio_get_level(PIN_BTN_BOOT) == 0;
    bool pwr = gpio_get_level(PIN_BTN_PWR) == 0;
    if (boot && !have_neutral) capture_neutral();

    if (pwr && !pwr_was_down) pwr_down_since = now;
    if (pwr && now - pwr_down_since >= 1000000) {
        ESP_LOGI(TAG, "power off");
        gpio_set_level(PIN_BAT_EN, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!pwr && pwr_was_down && now - pwr_down_since < 400000 && coin_seq == 0) {
        coin_seq = 1; coin_seq_start = now;
        capture_neutral();                    /* a coin also re-centres however you are holding it */
    }
    pwr_was_down = pwr;

    int64_t el = now - coin_seq_start;
    in->coin1 = 0; in->start1 = 0;
    switch (coin_seq) {
        case 1: in->coin1 = 1; if (el > 100000) coin_seq = 2; break;
        case 2: if (el > 500000) coin_seq = 3; break;
        case 3: in->start1 = 1; if (el > 600000) coin_seq = 0; break;
        default: break;
    }

    if (imu_ok && now - imu_last_us >= IMU_PERIOD_US) {
        imu_last_us = now;
        if (!have_neutral) capture_neutral();
        float lr, ud;
        read_angles(&lr, &ud);
        float roll = wrap_deg(lr - neutral_lr) * X_SIGN;
        float pitch = wrap_deg(ud - neutral_ud) * Y_SIGN;
        dbg_roll = roll; dbg_pitch = pitch;
        /* four-way: only the axis that is tilted further counts */
        dir_x = dir_y = 0;
        if (fabsf(roll) >= fabsf(pitch)) {
            if (roll > DIG_DEG) dir_x = +1; else if (roll < -DIG_DEG) dir_x = -1;
        } else {
            if (pitch > DIG_DEG) dir_y = +1; else if (pitch < -DIG_DEG) dir_y = -1;
        }
    }

    in->left  = (dir_x < 0) ? 1 : 0;
    in->right = (dir_x > 0) ? 1 : 0;
    in->up    = (dir_y > 0) ? 1 : 0;
    in->fire  = boot ? 1 : 0;                     /* the pump */
    in->down  = (dir_y < 0) ? 1 : 0;

    if (now - last_log >= 3000000) {
        last_log = now;
        ESP_LOGI(TAG, "imu %d  roll %+6.1f pitch %+6.1f -> U%d D%d L%d R%d F%d",
                 imu_ok, (double)dbg_roll, (double)dbg_pitch, in->up, in->down, in->left, in->right, in->fire);
    }
}
