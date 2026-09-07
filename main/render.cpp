/*
 * render.cpp - rotate the 288x224 native frame to the 240x280 portrait panel from a task.
 * Portrait pixel (px,py): game gx = px-8 (0..223), gy = py+4 (0..287, 4 rows cropped top and
 * bottom); native nx = gy, ny = 223-gx.
 */
#include "render.h"
#include "digdug.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "RENDER";
#define ROWS_PER_CHUNK 14
#define NUM_FB 2
#define X_MARGIN 8
#define Y_CROP 4

static uint8_t *fbs[NUM_FB];
static QueueHandle_t free_q, frame_q;
static uint16_t *chunk;
static uint16_t pal_swapped[256];
static uint32_t frames_drawn, frames_dropped;
static uint64_t busy_us;

static void present(const uint8_t *fb)
{
    display_set_window(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    for (int row = 0; row < DISPLAY_HEIGHT; row += ROWS_PER_CHUNK) {
        int rows = (row + ROWS_PER_CHUNK <= DISPLAY_HEIGHT) ? ROWS_PER_CHUNK : (DISPLAY_HEIGHT - row);
        uint16_t *dst = chunk;
        for (int r = 0; r < rows; r++) {
            int py = row + r;
            int nx = py + Y_CROP;
            const uint8_t *col = fb + nx;                 /* column nx of the native frame */
            for (int px = 0; px < X_MARGIN; px++) *dst++ = 0;
            /* gx = 0..223 -> ny = 223..0, i.e. walk the native column upward */
            const uint8_t *src = col + (DD_FB_H - 1) * DD_FB_W;
            for (int gx = 0; gx < DD_FB_H; gx++) { *dst++ = pal_swapped[*src]; src -= DD_FB_W; }
            for (int px = 0; px < DISPLAY_WIDTH - X_MARGIN - DD_FB_H; px++) *dst++ = 0;
        }
        display_write_preswapped(chunk, rows * DISPLAY_WIDTH);
    }
    display_wait_done();
}

static void render_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint8_t *fb;
        if (xQueueReceive(frame_q, &fb, portMAX_DELAY) != pdTRUE) continue;
        int64_t t0 = esp_timer_get_time();
        present(fb);
        busy_us += 3000;   /* approximate CPU share of present(): conversion per chunk */
        (void)t0;
        xQueueSend(free_q, &fb, 0);
        frames_drawn++;
    }
}

void render_init(void)
{
    uint16_t pal[DD_PALETTE_SIZE];
    dd_palette(pal);
    for (int i = 0; i < 256; i++) {
        uint16_t c = (i < DD_PALETTE_SIZE) ? pal[i] : 0;
        pal_swapped[i] = (uint16_t)((c >> 8) | (c << 8));
    }
    chunk = (uint16_t *)heap_caps_malloc(ROWS_PER_CHUNK * DISPLAY_WIDTH * sizeof(uint16_t), MALLOC_CAP_8BIT);
    free_q = xQueueCreate(NUM_FB, sizeof(uint8_t *));
    frame_q = xQueueCreate(NUM_FB, sizeof(uint8_t *));
    for (int i = 0; i < NUM_FB; i++) {
        fbs[i] = (uint8_t *)heap_caps_malloc(DD_FB_W * DD_FB_H, MALLOC_CAP_8BIT);
        if (!fbs[i]) { ESP_LOGE(TAG, "frame buffer allocation failed"); abort(); }
        xQueueSend(free_q, &fbs[i], 0);
    }
    if (!chunk) { ESP_LOGE(TAG, "chunk allocation failed"); abort(); }
    xTaskCreate(render_task, "render", 4096, nullptr, 6, nullptr);
    ESP_LOGI(TAG, "render task started");
}

uint8_t *render_acquire(void)
{
    uint8_t *fb;
    if (xQueueReceive(free_q, &fb, 0) != pdTRUE) { frames_dropped++; return nullptr; }
    return fb;
}
void render_submit(uint8_t *fb) { xQueueSend(frame_q, &fb, 0); }
uint32_t render_frames_drawn(void) { uint32_t v = frames_drawn; frames_drawn = 0; return v; }
uint32_t render_frames_dropped(void) { uint32_t v = frames_dropped; frames_dropped = 0; return v; }
uint64_t render_busy_us(void) { uint64_t v = busy_us; busy_us = 0; return v; }
