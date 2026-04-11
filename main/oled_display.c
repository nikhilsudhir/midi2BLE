/*
 * SSD1306 128×64 OLED display driver with status screen composition.
 *
 * Uses SPI (CLK/MOSI/DC/CS/RES).  The framebuffer is 8 pages × 128 columns
 * (1024 bytes).  Text is rendered using a built-in 5×7 bitmap font at 6 px per
 * character (5 px glyph + 1 px gap), giving 21 characters per line and 8 lines.
 *
 * NOTE: Some 1.3" modules labelled "SSD1306" actually use an SH1106 controller.
 * The SH1106 has a 132-column internal RAM with a 2-column offset and does not
 * support horizontal addressing mode.  If the display shows garbage or nothing,
 * switch to SH1106 page-mode writes.
 */

#include "oled_display.h"

#include <string.h>
#include <stdio.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "OLED"

// ---------------------------------------------------------------------------
// Hardware constants
// ---------------------------------------------------------------------------

#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64
#define SSD1306_PAGES   8    // HEIGHT / 8

#define FONT_W  5   // glyph pixel width
#define FONT_H  7   // glyph pixel height (fits in one 8-px page)
#define CHAR_W  6   // advance width (glyph + 1 px gap)
#define CHARS_PER_LINE  (SSD1306_WIDTH / CHAR_W)  // 21

// ---------------------------------------------------------------------------
// 5×7 bitmap font, ASCII 0x20–0x7E
// Each entry is 5 bytes — one byte per column, bit 0 = top pixel.
// Data compatible with the Adafruit GFX default font (glcdfont.c).
// ---------------------------------------------------------------------------

static const uint8_t s_font[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 0x20 ' '
    {0x00,0x00,0x5F,0x00,0x00}, // 0x21 '!'
    {0x00,0x07,0x00,0x07,0x00}, // 0x22 '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // 0x23 '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // 0x24 '$'
    {0x23,0x13,0x08,0x64,0x62}, // 0x25 '%'
    {0x36,0x49,0x55,0x22,0x50}, // 0x26 '&'
    {0x00,0x05,0x03,0x00,0x00}, // 0x27 '\''
    {0x00,0x1C,0x22,0x41,0x00}, // 0x28 '('
    {0x00,0x41,0x22,0x1C,0x00}, // 0x29 ')'
    {0x14,0x08,0x3E,0x08,0x14}, // 0x2A '*'
    {0x08,0x08,0x3E,0x08,0x08}, // 0x2B '+'
    {0x00,0x50,0x30,0x00,0x00}, // 0x2C ','
    {0x08,0x08,0x08,0x08,0x08}, // 0x2D '-'
    {0x00,0x60,0x60,0x00,0x00}, // 0x2E '.'
    {0x20,0x10,0x08,0x04,0x02}, // 0x2F '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // 0x30 '0'
    {0x00,0x42,0x7F,0x40,0x00}, // 0x31 '1'
    {0x42,0x61,0x51,0x49,0x46}, // 0x32 '2'
    {0x21,0x41,0x45,0x4B,0x31}, // 0x33 '3'
    {0x18,0x14,0x12,0x7F,0x10}, // 0x34 '4'
    {0x27,0x45,0x45,0x45,0x39}, // 0x35 '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // 0x36 '6'
    {0x01,0x71,0x09,0x05,0x03}, // 0x37 '7'
    {0x36,0x49,0x49,0x49,0x36}, // 0x38 '8'
    {0x06,0x49,0x49,0x29,0x1E}, // 0x39 '9'
    {0x00,0x36,0x36,0x00,0x00}, // 0x3A ':'
    {0x00,0x56,0x36,0x00,0x00}, // 0x3B ';'
    {0x08,0x14,0x22,0x41,0x00}, // 0x3C '<'
    {0x14,0x14,0x14,0x14,0x14}, // 0x3D '='
    {0x00,0x41,0x22,0x14,0x08}, // 0x3E '>'
    {0x02,0x01,0x51,0x09,0x06}, // 0x3F '?'
    {0x32,0x49,0x79,0x41,0x3E}, // 0x40 '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 0x41 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 0x42 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 0x43 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 0x44 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 0x45 'E'
    {0x7F,0x09,0x09,0x09,0x01}, // 0x46 'F'
    {0x3E,0x41,0x49,0x49,0x7A}, // 0x47 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 0x48 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 0x49 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 0x4A 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 0x4B 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 0x4C 'L'
    {0x7F,0x02,0x0C,0x02,0x7F}, // 0x4D 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 0x4E 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 0x4F 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 0x50 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 0x51 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 0x52 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 0x53 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 0x54 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 0x55 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 0x56 'V'
    {0x3F,0x40,0x38,0x40,0x3F}, // 0x57 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 0x58 'X'
    {0x07,0x08,0x70,0x08,0x07}, // 0x59 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 0x5A 'Z'
    {0x00,0x7F,0x41,0x41,0x00}, // 0x5B '['
    {0x02,0x04,0x08,0x10,0x20}, // 0x5C '\\'
    {0x00,0x41,0x41,0x7F,0x00}, // 0x5D ']'
    {0x04,0x02,0x01,0x02,0x04}, // 0x5E '^'
    {0x40,0x40,0x40,0x40,0x40}, // 0x5F '_'
    {0x00,0x01,0x02,0x04,0x00}, // 0x60 '`'
    {0x20,0x54,0x54,0x54,0x78}, // 0x61 'a'
    {0x7F,0x48,0x44,0x44,0x38}, // 0x62 'b'
    {0x38,0x44,0x44,0x44,0x20}, // 0x63 'c'
    {0x38,0x44,0x44,0x48,0x7F}, // 0x64 'd'
    {0x38,0x54,0x54,0x54,0x18}, // 0x65 'e'
    {0x08,0x7E,0x09,0x01,0x02}, // 0x66 'f'
    {0x0C,0x52,0x52,0x52,0x3E}, // 0x67 'g'
    {0x7F,0x08,0x04,0x04,0x78}, // 0x68 'h'
    {0x00,0x44,0x7D,0x40,0x00}, // 0x69 'i'
    {0x20,0x40,0x44,0x3D,0x00}, // 0x6A 'j'
    {0x7F,0x10,0x28,0x44,0x00}, // 0x6B 'k'
    {0x00,0x41,0x7F,0x40,0x00}, // 0x6C 'l'
    {0x7C,0x04,0x18,0x04,0x78}, // 0x6D 'm'
    {0x7C,0x08,0x04,0x04,0x78}, // 0x6E 'n'
    {0x38,0x44,0x44,0x44,0x38}, // 0x6F 'o'
    {0x7C,0x14,0x14,0x14,0x08}, // 0x70 'p'
    {0x08,0x14,0x14,0x18,0x7C}, // 0x71 'q'
    {0x7C,0x08,0x04,0x04,0x08}, // 0x72 'r'
    {0x48,0x54,0x54,0x54,0x20}, // 0x73 's'
    {0x04,0x3F,0x44,0x40,0x20}, // 0x74 't'
    {0x3C,0x40,0x40,0x20,0x7C}, // 0x75 'u'
    {0x1C,0x20,0x40,0x20,0x1C}, // 0x76 'v'
    {0x3C,0x40,0x30,0x40,0x3C}, // 0x77 'w'
    {0x44,0x28,0x10,0x28,0x44}, // 0x78 'x'
    {0x0C,0x50,0x50,0x50,0x3C}, // 0x79 'y'
    {0x44,0x64,0x54,0x4C,0x44}, // 0x7A 'z'
    {0x00,0x08,0x36,0x41,0x00}, // 0x7B '{'
    {0x00,0x00,0x7F,0x00,0x00}, // 0x7C '|'
    {0x00,0x41,0x36,0x08,0x00}, // 0x7D '}'
    {0x10,0x08,0x08,0x10,0x08}, // 0x7E '~'
};

// ---------------------------------------------------------------------------
// Framebuffer & hardware handles
// ---------------------------------------------------------------------------

static uint8_t s_fb[SSD1306_PAGES][SSD1306_WIDTH];  // [page][column]

static spi_device_handle_t s_spi = NULL;
static bool s_ok = false;  // set true only if init succeeded

// ---------------------------------------------------------------------------
// Display state (written from any task; read in oled_update)
// ---------------------------------------------------------------------------

static device_mode_t s_mode        = MODE_WIRELESS;
static bool          s_ble_conn    = false;
static bool          s_usb_conn    = false;
static bool          s_midi_active = false;
static uint8_t       s_bat_pct     = 0;
static char          s_usb_name[44] = {0};  // up to 2 lines of 21 chars

// ---------------------------------------------------------------------------
// SSD1306 low-level helpers
// ---------------------------------------------------------------------------

static void spi_send(const uint8_t *data, size_t len, bool is_data)
{
    gpio_set_level(OLED_DC, is_data ? 1 : 0);
    spi_transaction_t t = {
        .length   = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void ssd1306_send_cmds(const uint8_t *cmds, size_t len)
{
    spi_send(cmds, len, false);
}

static void ssd1306_flush(void)
{
    // SH1106 uses page-addressing only, with a 2-column internal RAM offset.
    // Writing page-by-page works on both SH1106 and SSD1306.
    // If display is shifted 2 px left, change OLED_COL_OFFSET to 0.
    #define OLED_COL_OFFSET 2
    for (int page = 0; page < SSD1306_PAGES; page++) {
        uint8_t cmds[] = {
            0xB0 | page,                          // set page
            0x00 | (OLED_COL_OFFSET & 0x0F),      // column low nibble
            0x10 | ((OLED_COL_OFFSET >> 4) & 0x0F), // column high nibble
        };
        ssd1306_send_cmds(cmds, sizeof(cmds));
        spi_send(s_fb[page], SSD1306_WIDTH, true);
    }
    #undef OLED_COL_OFFSET
}

// ---------------------------------------------------------------------------
// Framebuffer drawing primitives
// ---------------------------------------------------------------------------

static void fb_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

/** Draw one glyph at pixel column x, text page (0–7). */
static void fb_char(int page, int x, char c)
{
    if (c < 0x20 || c > 0x7E) c = ' ';
    if (page < 0 || page >= SSD1306_PAGES) return;
    const uint8_t *g = s_font[(uint8_t)c - 0x20];
    for (int col = 0; col < FONT_W; col++) {
        if (x + col >= SSD1306_WIDTH) break;
        s_fb[page][x + col] = g[col];
    }
}

/** Draw a NUL-terminated string starting at pixel column x on text page. */
static void fb_str(int page, int x, const char *str)
{
    while (*str && x < SSD1306_WIDTH) {
        fb_char(page, x, *str++);
        x += CHAR_W;
    }
}

/** Draw a string horizontally centred on page. */
static void fb_str_center(int page, const char *str)
{
    int len = (int)strlen(str);
    int x   = (SSD1306_WIDTH - len * CHAR_W) / 2;
    if (x < 0) x = 0;
    fb_str(page, x, str);
}

/** Draw a battery bar on the given page: "BAT: [====    ]" */
static void fb_battery_bar(int page)
{
#define BAR_CELLS  10
    char buf[22] = "BAT: [          ]";
    int filled = (s_bat_pct * BAR_CELLS) / 100;
    for (int i = 0; i < filled && i < BAR_CELLS; i++) {
        buf[6 + i] = '=';
    }
    fb_str_center(page, buf);
#undef BAR_CELLS
}

// ---------------------------------------------------------------------------
// Screen compositions
// ---------------------------------------------------------------------------

static void compose_wireless(void)
{
    fb_str_center(0, "MIDI2BLE - BLE");
    fb_str_center(1, s_ble_conn ? "BLE: CONNECTED" : "BLE: SEARCHING");

    fb_battery_bar(6);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", s_bat_pct);
    fb_str_center(7, pct);
}

static void compose_passthrough(void)
{
    fb_str_center(0, "MIDI2BLE - WIRED");
    fb_str_center(1, s_usb_conn ? "USB: CONNECTED" : "USB: NOT CONNECTED");

    if (s_usb_conn && s_usb_name[0]) {
        fb_str_center(5, s_usb_name);
    }

    fb_battery_bar(6);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", s_bat_pct);
    fb_str_center(7, pct);
}

// ---------------------------------------------------------------------------
// SSD1306 initialisation sequence
// ---------------------------------------------------------------------------

static void ssd1306_init_panel(void)
{
    static const uint8_t init_cmds[] = {
        0xAE,        // Display OFF
        0xD5, 0x80,  // Display clock divide ratio / oscillator frequency
        0xA8, 0x3F,  // Multiplex ratio: 63 (64 rows)
        0xD3, 0x00,  // Display offset: 0
        0x40,        // Display start line: 0
        0xAD, 0x8B,  // Charge pump: enable (SH1106)
        0xA0,        // Segment remap: col 0 → SEG0 (flipped horizontally)
        0xC0,        // COM scan direction: normal (flipped vertically)
        0xDA, 0x12,  // COM pin config: alternative, no remap
                     // Change to 0x02 if display is vertically misaligned
        0x81, 0xCF,  // Contrast: 0xCF
        0xD9, 0xF1,  // Pre-charge period
        0xDB, 0x40,  // VCOMH deselect level
        0xA4,        // Display on: follow GDDRAM content
        0xA6,        // Normal display (not inverted)
        0xAF,        // Display ON
    };
    ssd1306_send_cmds(init_cmds, sizeof(init_cmds));
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void oled_init(void)
{
    // DC and RES are plain GPIOs, not managed by the SPI peripheral
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << OLED_DC) | (1ULL << OLED_RES),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_cfg);

    // Hardware reset: hold low >1 ms, then release
    gpio_set_level(OLED_RES, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(OLED_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    // SPI bus (MISO not used)
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = OLED_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = OLED_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = SSD1306_WIDTH * SSD1306_PAGES + 16,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return;
    }

    // SSD1306 SPI device — mode 0, 10 MHz
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode           = 0,
        .spics_io_num   = OLED_CS,
        .queue_size     = 1,
    };
    err = spi_bus_add_device(SPI2_HOST, &dev_cfg, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        return;
    }

    ssd1306_init_panel();
    fb_clear();
    ssd1306_flush();

    s_ok = true;
    ESP_LOGI(TAG, "SSD1306 ready");
}

void oled_set_mode(device_mode_t mode)           { s_mode     = mode;     }
void oled_set_ble_connected(bool c)              { s_ble_conn = c;        }
void oled_set_midi_active(bool a)                { s_midi_active = a;     }
void oled_set_battery(uint8_t pct)               { s_bat_pct  = pct;      }

void oled_set_usb_connected(bool c, const char *name)
{
    s_usb_conn = c;
    if (!c || !name) {
        s_usb_name[0] = '\0';
    } else {
        strncpy(s_usb_name, name, sizeof(s_usb_name) - 1);
        s_usb_name[sizeof(s_usb_name) - 1] = '\0';
    }
}

void oled_show_mode_change(void)
{
    if (!s_ok) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        return;
    }
    fb_clear();
    fb_str_center(2, "MODE CHANGED");
    fb_str_center(3, "RESTARTING...");
    ssd1306_flush();
    vTaskDelay(pdMS_TO_TICKS(2000));
}

void oled_update(void)
{
    if (!s_ok) return;
    fb_clear();
    if (s_mode == MODE_WIRELESS) {
        compose_wireless();
    } else {
        compose_passthrough();
    }
    ssd1306_flush();
}
