#include "ssh1106.h"
#include "font.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdlib.h>

#define SSH1106_CMD_SET_CONTRAST           0x81
#define SSH1106_CMD_DISPLAY_ALL_ON_RESUME  0xA4
#define SSH1106_CMD_DISPLAY_ALL_ON         0xA5
#define SSH1106_CMD_NORMAL_DISPLAY         0xA6
#define SSH1106_CMD_INVERT_DISPLAY         0xA7
#define SSH1106_CMD_DISPLAY_OFF            0xAE
#define SSH1106_CMD_DISPLAY_ON             0xAF
#define SSH1106_CMD_SET_DISPLAY_OFFSET     0xD3
#define SSH1106_CMD_SET_COMPINS            0xDA
#define SSH1106_CMD_SET_VCOM_DETECT        0xDB
#define SSH1106_CMD_SET_DISPLAY_CLOCK_DIV  0xD5
#define SSH1106_CMD_SET_PRECHARGE          0xD9
#define SSH1106_CMD_SET_MULTIPLEX          0xA8
#define SSH1106_CMD_SET_LOW_COLUMN         0x00  // + column[3:0]
#define SSH1106_CMD_SET_HIGH_COLUMN        0x10  // + column[7:4]
#define SSH1106_CMD_SET_START_LINE         0x40  // + line[5:0]
#define SSH1106_CMD_SET_PAGE_ADDR          0xB0  // + page[2:0]
#define SSH1106_CMD_SET_SEGMENT_REMAP_0    0xA0
#define SSH1106_CMD_SET_SEGMENT_REMAP_127  0xA1
#define SSH1106_CMD_COM_SCAN_INC           0xC0
#define SSH1106_CMD_COM_SCAN_DEC           0xC8
#define SSH1106_CMD_DC_DC_CONTROL          0xAD
#define SSH1106_CMD_DC_DC_ON               0x8B

// Column offset for SSH1106 (try 0 if display shows horizontal lines)
#define SSH1106_COLUMN_OFFSET              0

// ============================================================================
// PRIVATE FUNCTIONS - I2C Communication Helpers
// ============================================================================

// Send single command to SSH1106
static void ssh1106_send_command(ssh1106_t *display, uint8_t cmd) {
    uint8_t buffer[2] = {0x00, cmd};  // Control byte + command
    i2c_write_blocking(display->i2c_port, display->i2c_address, buffer, 2, false);
}

// Send data buffer to SSH1106
static void ssh1106_send_data(ssh1106_t *display, const uint8_t *data, size_t len) {
    // Create buffer with control byte + data
    uint8_t buffer[len + 1];
    buffer[0] = 0x40;  // Data mode control byte
    memcpy(buffer + 1, data, len);
    i2c_write_blocking(display->i2c_port, display->i2c_address, buffer, len + 1, false);
}

// ============================================================================
// PUBLIC FUNCTIONS - Initialization & Control
// ============================================================================

bool ssh1106_init(ssh1106_t *display, i2c_inst_t *i2c_port, uint8_t sda_pin, uint8_t scl_pin) {
    // Store configuration
    display->i2c_port = i2c_port;
    display->i2c_address = SSH1106_I2C_ADDR;
    display->initialized = false;

    // Clear frame buffer immediately
    memset(display->framebuffer, 0x00, sizeof(display->framebuffer));

    // Initialize I2C at 400kHz (Fast Mode)
    i2c_init(i2c_port, 400 * 1000);

    // Configure GPIO pins for I2C
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);

    // Give display time to power up
    sleep_ms(100);

    // Initialization sequence
    ssh1106_send_command(display, SSH1106_CMD_DISPLAY_OFF);

    ssh1106_send_command(display, SSH1106_CMD_SET_DISPLAY_CLOCK_DIV);
    ssh1106_send_command(display, 0x80);  // Default ratio

    ssh1106_send_command(display, SSH1106_CMD_SET_MULTIPLEX);
    ssh1106_send_command(display, 0x3F);  // 1/64 duty

    ssh1106_send_command(display, SSH1106_CMD_SET_DISPLAY_OFFSET);
    ssh1106_send_command(display, 0x00);  // No offset

    ssh1106_send_command(display, SSH1106_CMD_SET_START_LINE | 0x00);

    ssh1106_send_command(display, SSH1106_CMD_DC_DC_CONTROL);
    ssh1106_send_command(display, SSH1106_CMD_DC_DC_ON);  // Enable charge pump

    ssh1106_send_command(display, SSH1106_CMD_SET_SEGMENT_REMAP_0);  // Normal segment mapping
    ssh1106_send_command(display, SSH1106_CMD_COM_SCAN_INC);         // Normal COM scan

    ssh1106_send_command(display, SSH1106_CMD_SET_COMPINS);
    ssh1106_send_command(display, 0x12);  // Alternative COM config

    ssh1106_send_command(display, SSH1106_CMD_SET_CONTRAST);
    ssh1106_send_command(display, 0x7F);  // Mid-range contrast

    ssh1106_send_command(display, SSH1106_CMD_SET_PRECHARGE);
    ssh1106_send_command(display, 0x22);  // Default precharge

    ssh1106_send_command(display, SSH1106_CMD_SET_VCOM_DETECT);
    ssh1106_send_command(display, 0x35);  // 0.77 * Vcc

    ssh1106_send_command(display, SSH1106_CMD_DISPLAY_ALL_ON_RESUME);
    ssh1106_send_command(display, SSH1106_CMD_NORMAL_DISPLAY);

    // Clear frame buffer and send to display
    ssh1106_clear(display);

    // Clear display RAM by writing zeros to all pages
    for (uint8_t page = 0; page < 8; page++) {
        ssh1106_send_command(display, SSH1106_CMD_SET_PAGE_ADDR | page);
        ssh1106_send_command(display, 0x00);  // Set low column
        ssh1106_send_command(display, 0x10);  // Set high column

        // Send 132 bytes of zeros (SSH1106 has 132 columns)
        uint8_t zeros[132] = {0};
        ssh1106_send_data(display, zeros, 132);
    }

    ssh1106_update(display);

    sleep_ms(10);

    ssh1106_send_command(display, SSH1106_CMD_DISPLAY_ON);

    display->initialized = true;
    return true;
}

void ssh1106_display_on(ssh1106_t *display) {
    ssh1106_send_command(display, SSH1106_CMD_DISPLAY_ON);
}

void ssh1106_display_off(ssh1106_t *display) {
    ssh1106_send_command(display, SSH1106_CMD_DISPLAY_OFF);
}

void ssh1106_set_contrast(ssh1106_t *display, uint8_t contrast) {
    ssh1106_send_command(display, SSH1106_CMD_SET_CONTRAST);
    ssh1106_send_command(display, contrast);  // 0-255
}

void ssh1106_invert_display(ssh1106_t *display, bool invert) {
    ssh1106_send_command(display, invert ? SSH1106_CMD_INVERT_DISPLAY : SSH1106_CMD_NORMAL_DISPLAY);
}

// ============================================================================
// PUBLIC FUNCTIONS - Buffer Management
// ============================================================================

void ssh1106_clear(ssh1106_t *display) {
    memset(display->framebuffer, 0x00, sizeof(display->framebuffer));
}

void ssh1106_fill(ssh1106_t *display, uint8_t color) {
    memset(display->framebuffer, color ? 0xFF : 0x00, sizeof(display->framebuffer));
}

void ssh1106_update(ssh1106_t *display) {
    for (uint8_t page = 0; page < SSH1106_PAGES; page++) {
        // Set page address
        ssh1106_send_command(display, SSH1106_CMD_SET_PAGE_ADDR | page);

        // Set column address (with offset)
        ssh1106_send_command(display, SSH1106_CMD_SET_LOW_COLUMN | (SSH1106_COLUMN_OFFSET & 0x0F));
        ssh1106_send_command(display, SSH1106_CMD_SET_HIGH_COLUMN | ((SSH1106_COLUMN_OFFSET >> 4) & 0x0F));

        // Send page data (128 bytes)
        ssh1106_send_data(display, &display->framebuffer[page * SSH1106_WIDTH], SSH1106_WIDTH);
    }
}

// ============================================================================
// PUBLIC FUNCTIONS - Graphics Primitives
// ============================================================================

void ssh1106_draw_pixel(ssh1106_t *display, int16_t x, int16_t y, uint8_t color) {
    // Bounds checking
    if (x < 0 || x >= SSH1106_WIDTH || y < 0 || y >= SSH1106_HEIGHT) {
        return;
    }

    // Calculate buffer position
    // Page = y / 8, Bit position = y % 8
    uint16_t index = x + (y / 8) * SSH1106_WIDTH;
    uint8_t bit_mask = 1 << (y % 8);

    if (color) {
        display->framebuffer[index] |= bit_mask;   // Set bit
    } else {
        display->framebuffer[index] &= ~bit_mask;  // Clear bit
    }
}

uint8_t ssh1106_get_pixel(ssh1106_t *display, int16_t x, int16_t y) {
    if (x < 0 || x >= SSH1106_WIDTH || y < 0 || y >= SSH1106_HEIGHT) {
        return 0;
    }
    uint16_t index = x + (y / 8) * SSH1106_WIDTH;
    uint8_t bit_mask = 1 << (y % 8);
    return (display->framebuffer[index] & bit_mask) ? 1 : 0;
}

void ssh1106_draw_line(ssh1106_t *display, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color) {
    // Bresenham's line algorithm
    int16_t dx = abs(x1 - x0);
    int16_t dy = abs(y1 - y0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;

    while (true) {
        ssh1106_draw_pixel(display, x0, y0, color);

        if (x0 == x1 && y0 == y1) break;

        int16_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void ssh1106_draw_rect(ssh1106_t *display, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
    ssh1106_draw_line(display, x, y, x + w - 1, y, color);           // Top
    ssh1106_draw_line(display, x, y + h - 1, x + w - 1, y + h - 1, color); // Bottom
    ssh1106_draw_line(display, x, y, x, y + h - 1, color);           // Left
    ssh1106_draw_line(display, x + w - 1, y, x + w - 1, y + h - 1, color); // Right
}

void ssh1106_fill_rect(ssh1106_t *display, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
    for (int16_t i = x; i < x + w; i++) {
        for (int16_t j = y; j < y + h; j++) {
            ssh1106_draw_pixel(display, i, j, color);
        }
    }
}

void ssh1106_draw_circle(ssh1106_t *display, int16_t x0, int16_t y0, int16_t r, uint8_t color) {
    // Midpoint circle algorithm
    int16_t x = r;
    int16_t y = 0;
    int16_t err = 0;

    while (x >= y) {
        ssh1106_draw_pixel(display, x0 + x, y0 + y, color);
        ssh1106_draw_pixel(display, x0 + y, y0 + x, color);
        ssh1106_draw_pixel(display, x0 - y, y0 + x, color);
        ssh1106_draw_pixel(display, x0 - x, y0 + y, color);
        ssh1106_draw_pixel(display, x0 - x, y0 - y, color);
        ssh1106_draw_pixel(display, x0 - y, y0 - x, color);
        ssh1106_draw_pixel(display, x0 + y, y0 - x, color);
        ssh1106_draw_pixel(display, x0 + x, y0 - y, color);

        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }
}

void ssh1106_fill_circle(ssh1106_t *display, int16_t x0, int16_t y0, int16_t r, uint8_t color) {
    for (int16_t y = -r; y <= r; y++) {
        for (int16_t x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                ssh1106_draw_pixel(display, x0 + x, y0 + y, color);
            }
        }
    }
}

// ============================================================================
// PUBLIC FUNCTIONS - Text Rendering
// ============================================================================

void ssh1106_draw_char(ssh1106_t *display, int16_t x, int16_t y, char c, uint8_t color) {
    if (c < 32 || c > 126) {
        return;  // Only printable ASCII
    }

    const uint8_t *glyph = font5x7[c - 32];

    for (uint8_t col = 0; col < FONT_WIDTH; col++) {
        uint8_t column_data = glyph[col];
        for (uint8_t row = 0; row < FONT_HEIGHT; row++) {
            if (column_data & (1 << row)) {
                ssh1106_draw_pixel(display, x + col, y + row, color);
            }
        }
    }
}

void ssh1106_draw_string(ssh1106_t *display, int16_t x, int16_t y, const char *str, uint8_t color) {
    int16_t cursor_x = x;
    while (*str) {
        ssh1106_draw_char(display, cursor_x, y, *str, color);
        cursor_x += FONT_WIDTH + 1;  // Character width + 1px spacing
        str++;
    }
}

uint16_t ssh1106_get_string_width(const char *str) {
    size_t len = strlen(str);
    return len > 0 ? (len * (FONT_WIDTH + 1) - 1) : 0;  // Remove trailing space
}
