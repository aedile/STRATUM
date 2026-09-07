#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void render_init(void);
/* returns a free frame buffer (DD_FB_W*DD_FB_H bytes) or NULL if the renderer is busy */
uint8_t *render_acquire(void);
void render_submit(uint8_t *fb);          /* hand a filled buffer to the render task */
uint32_t render_frames_drawn(void);
uint32_t render_frames_dropped(void);
uint64_t render_busy_us(void);
#ifdef __cplusplus
}
#endif
