#ifndef SSH1106_H
#define SSH1106_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

// Display dimensions
#define SSH1106_WIDTH           128
#define SSH1106_HEIGHT          64
#define SSH1106_PAGES           8
#define SSH1106_I2C_ADDR        0x3C

// Color definitions
#define SSH1106_COLOR_BLACK     0
#define SSH1106_COLOR_WHITE     1

// Main device structure
typedef struct {
    i2c_inst_t *i2c_port;           // I2C instance (i2c0 or i2c1)
    uint8_t i2c_address;             // Device address
    uint8_t framebuffer[1024];       // 128 * 64 / 8 = 1024 bytes
    bool initialized;                // Initialization status
} ssh1106_t;

// Initialization & Control
bool ssh1106_init(ssh1106_t *display, i2c_inst_t *i2c_port, uint8_t sda_pin, uint8_t scl_pin);
void ssh1106_display_on(ssh1106_t *display);
void ssh1106_display_off(ssh1106_t *display);
void ssh1106_set_contrast(ssh1106_t *display, uint8_t contrast);
void ssh1106_invert_display(ssh1106_t *display, bool invert);

// Buffer Management
void ssh1106_clear(ssh1106_t *display);
void ssh1106_fill(ssh1106_t *display, uint8_t color);
void ssh1106_update(ssh1106_t *display);

// Graphics Primitives
void ssh1106_draw_pixel(ssh1106_t *display, int16_t x, int16_t y, uint8_t color);
uint8_t ssh1106_get_pixel(ssh1106_t *display, int16_t x, int16_t y);
void ssh1106_draw_line(ssh1106_t *display, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color);
void ssh1106_draw_rect(ssh1106_t *display, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void ssh1106_fill_rect(ssh1106_t *display, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void ssh1106_draw_circle(ssh1106_t *display, int16_t x0, int16_t y0, int16_t r, uint8_t color);
void ssh1106_fill_circle(ssh1106_t *display, int16_t x0, int16_t y0, int16_t r, uint8_t color);

// Text Rendering
void ssh1106_draw_char(ssh1106_t *display, int16_t x, int16_t y, char c, uint8_t color);
void ssh1106_draw_string(ssh1106_t *display, int16_t x, int16_t y, const char *str, uint8_t color);
uint16_t ssh1106_get_string_width(const char *str);

#endif // SSH1106_H
