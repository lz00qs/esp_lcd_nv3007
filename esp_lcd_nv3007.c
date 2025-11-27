#include <stdlib.h>
#include <sys/cdefs.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

#include "esp_lcd_nv3007.h"

static const char *TAG = "lcd_panel.nv3007";

typedef struct
{
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    bool reset_level;
    int x_gap;
    int y_gap;
    uint8_t fb_bits_per_pixel;
    uint8_t madctl_val; // save current value of LCD_CMD_MADCTL register
    uint8_t colmod_val; // save current value of LCD_CMD_COLMOD register
    const nv3007_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
} nv3007_panel_t;

static esp_err_t panel_nv3007_del(esp_lcd_panel_t *panel)
{
    nv3007_panel_t *nv3007 = __containerof(panel, nv3007_panel_t, base);

    if (nv3007->reset_gpio_num >= 0)
    {
        gpio_reset_pin(nv3007->reset_gpio_num);
    }
    ESP_LOGD(TAG, "del nv3007 panel @%p", nv3007);
    free(nv3007);
    return ESP_OK;
}

static esp_err_t panel_nv3007_reset(esp_lcd_panel_t *panel)
{
    nv3007_panel_t *nv3007 = __containerof(panel, nv3007_panel_t, base);
    esp_lcd_panel_io_handle_t io = nv3007->io;

    // perform hardware reset
    if (nv3007->reset_gpio_num >= 0)
    {
        gpio_set_level(nv3007->reset_gpio_num, nv3007->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(nv3007->reset_gpio_num, !nv3007->reset_level);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    else
    { // perform software reset
        esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    return ESP_OK;
}

static const nv3007_lcd_init_cmd_t vendor_specific_init_default[] = {
    //  {cmd, { data }, data_size, delay_ms}
    // {0xfd, (uint8_t []){0x06, 0x08}, 2, 0},
    {0xff, (uint8_t[]){0xa5}, 1, 0},
    {0x9a, (uint8_t[]){0x08}, 1, 0},
    {0x9b, (uint8_t[]){0x08}, 1, 0},
    {0x9c, (uint8_t[]){0xb0}, 1, 0},
    {0x9d, (uint8_t[]){0x16}, 1, 0},
    {0x9e, (uint8_t[]){0xc4}, 1, 0},
    {0x8f, (uint8_t[]){0x55, 0x04}, 2, 0},
    {0x84, (uint8_t[]){0x90}, 1, 0},
    {0x83, (uint8_t[]){0x7b}, 1, 0},
    {0x85, (uint8_t[]){0x33}, 1, 0},
    {0x60, (uint8_t[]){0x00}, 1, 0},
    {0x70, (uint8_t[]){0x00}, 1, 0},
    {0x61, (uint8_t[]){0x02}, 1, 0},
    {0x71, (uint8_t[]){0x02}, 1, 0},
    {0x62, (uint8_t[]){0x04}, 1, 0},
    {0x72, (uint8_t[]){0x04}, 1, 0},
    {0x6c, (uint8_t[]){0x29}, 1, 0},
    {0x7c, (uint8_t[]){0x29}, 1, 0},
    {0x6d, (uint8_t[]){0x31}, 1, 0},
    {0x7d, (uint8_t[]){0x31}, 1, 0},
    {0x6e, (uint8_t[]){0x0f}, 1, 0},
    {0x7e, (uint8_t[]){0x0f}, 1, 0},
    {0x66, (uint8_t[]){0x21}, 1, 0},
    {0x76, (uint8_t[]){0x21}, 1, 0},
    {0x68, (uint8_t[]){0x3a}, 1, 0},
    {0x78, (uint8_t[]){0x3a}, 1, 0},
    {0x63, (uint8_t[]){0x07}, 1, 0},
    {0x73, (uint8_t[]){0x07}, 1, 0},
    {0x64, (uint8_t[]){0x05}, 1, 0},
    {0x74, (uint8_t[]){0x05}, 1, 0},
    {0x65, (uint8_t[]){0x02}, 1, 0},
    {0x75, (uint8_t[]){0x02}, 1, 0},
    {0x67, (uint8_t[]){0x23}, 1, 0},
    {0x77, (uint8_t[]){0x23}, 1, 0},
    {0x69, (uint8_t[]){0x08}, 1, 0},
    {0x79, (uint8_t[]){0x08}, 1, 0},
    {0x6a, (uint8_t[]){0x13}, 1, 0},
    {0x7a, (uint8_t[]){0x13}, 1, 0},
    {0x6b, (uint8_t[]){0x13}, 1, 0},
    {0x7b, (uint8_t[]){0x13}, 1, 0},
    {0x6f, (uint8_t[]){0x00}, 1, 0},
    {0x7f, (uint8_t[]){0x00}, 1, 0},
    {0x50, (uint8_t[]){0x00}, 1, 0},
    {0x52, (uint8_t[]){0xd6}, 1, 0},
    {0x53, (uint8_t[]){0x08}, 1, 0},
    {0x54, (uint8_t[]){0x08}, 1, 0},
    {0x55, (uint8_t[]){0x1e}, 1, 0},
    {0x56, (uint8_t[]){0x1c}, 1, 0},

    // goa map_sel
    {0xa0, (uint8_t[]){0x2b, 0x24, 0x00}, 3, 0},
    {0xa1, (uint8_t[]){0x87}, 1, 0},
    {0xa2, (uint8_t[]){0x86}, 1, 0},
    {0xa5, (uint8_t[]){0x00}, 1, 0},
    {0xa6, (uint8_t[]){0x00}, 1, 0},
    {0xa7, (uint8_t[]){0x00}, 1, 0},
    {0xa8, (uint8_t[]){0x36}, 1, 0},
    {0xa9, (uint8_t[]){0x7e}, 1, 0},
    {0xaa, (uint8_t[]){0x7e}, 1, 0},
    {0xb9, (uint8_t[]){0x85}, 1, 0},
    {0xba, (uint8_t[]){0x84}, 1, 0},
    {0xbb, (uint8_t[]){0x83}, 1, 0},
    {0xbc, (uint8_t[]){0x82}, 1, 0},
    {0xbd, (uint8_t[]){0x81}, 1, 0},
    {0xbe, (uint8_t[]){0x80}, 1, 0},
    {0xbf, (uint8_t[]){0x01}, 1, 0},
    {0xc0, (uint8_t[]){0x02}, 1, 0},
    {0xc1, (uint8_t[]){0x00}, 1, 0},
    {0xc2, (uint8_t[]){0x00}, 1, 0},
    {0xc3, (uint8_t[]){0x00}, 1, 0},
    {0xc4, (uint8_t[]){0x33}, 1, 0},
    {0xc5, (uint8_t[]){0x7e}, 1, 0},
    {0xc6, (uint8_t[]){0x7e}, 1, 0},
    {0xc8, (uint8_t[]){0x33, 0x33}, 2, 0},
    {0xc9, (uint8_t[]){0x68}, 1, 0},
    {0xca, (uint8_t[]){0x69}, 1, 0},
    {0xcb, (uint8_t[]){0x6a}, 1, 0},
    {0xcc, (uint8_t[]){0x6b}, 1, 0},
    {0xcd, (uint8_t[]){0x33, 0x33}, 2, 0},
    {0xce, (uint8_t[]){0x6c}, 1, 0},
    {0xcf, (uint8_t[]){0x6d}, 1, 0},
    {0xd0, (uint8_t[]){0x6e}, 1, 0},
    {0xd1, (uint8_t[]){0x6f}, 1, 0},
    {0xab, (uint8_t[]){0x03, 0x67}, 2, 0},
    {0xac, (uint8_t[]){0x03, 0x6b}, 2, 0},
    {0xad, (uint8_t[]){0x03, 0x68}, 2, 0},
    {0xae, (uint8_t[]){0x03, 0x6c}, 2, 0},
    {0xb3, (uint8_t[]){0x00}, 1, 0},
    {0xb4, (uint8_t[]){0x00}, 1, 0},
    {0xb5, (uint8_t[]){0x00}, 1, 0},
    {0xb6, (uint8_t[]){0x32}, 1, 0},
    {0xb7, (uint8_t[]){0x7e}, 1, 0},
    {0xb8, (uint8_t[]){0x7e}, 1, 0},
    {0xe0, (uint8_t[]){0x00}, 1, 0},
    {0xe1, (uint8_t[]){0x03, 0x0f}, 2, 0},
    {0xe2, (uint8_t[]){0x04}, 1, 0},
    {0xe3, (uint8_t[]){0x01}, 1, 0},
    {0xe4, (uint8_t[]){0x0e}, 1, 0},
    {0xe5, (uint8_t[]){0x01}, 1, 0},
    {0xe6, (uint8_t[]){0x19}, 1, 0},
    {0xe7, (uint8_t[]){0x10}, 1, 0},
    {0xe8, (uint8_t[]){0x10}, 1, 0},
    {0xea, (uint8_t[]){0x12}, 1, 0},
    {0xeb, (uint8_t[]){0xd0}, 1, 0},
    {0xec, (uint8_t[]){0x04}, 1, 0},
    {0xed, (uint8_t[]){0x07}, 1, 0},
    {0xee, (uint8_t[]){0x07}, 1, 0},
    {0xef, (uint8_t[]){0x09}, 1, 0},
    {0xf0, (uint8_t[]){0xd0}, 1, 0},
    {0xf1, (uint8_t[]){0x0e, 0x17}, 2, 0},
    {0xf2, (uint8_t[]){0x2c, 0x1b, 0x0b, 0x20}, 4, 0},

    // 1dot
    {0xe9, (uint8_t[]){0x29}, 1, 0},
    {0xec, (uint8_t[]){0x04}, 1, 0},

    // te
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x44, (uint8_t[]){0x00, 0x10}, 2, 0},
    {0x46, (uint8_t[]){0x10}, 1, 0},
    {0xff, (uint8_t[]){0x00}, 1, 0},

    // direction
    // {0x36, (uint8_t[]){0x00}, 1, 0},

    {0x11, (uint8_t[]){0}, 0, 220},
    {0x29, (uint8_t[]){0}, 0, 200},
};

static esp_err_t panel_nv3007_init(esp_lcd_panel_t *panel)
{
    nv3007_panel_t *nv3007 = __containerof(panel, nv3007_panel_t, base);
    esp_lcd_panel_io_handle_t io = nv3007->io;

    // LCD goes into sleep mode and display will be turned off after power on reset, exit sleep mode first
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SLPOUT, NULL, 0), TAG, "send command failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]){
                                                                          nv3007->madctl_val,
                                                                      },
                                                  1),
                        TAG, "send command failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_COLMOD, (uint8_t[]){
                                                                          nv3007->colmod_val,
                                                                      },
                                                  1),
                        TAG, "send command failed");

    const nv3007_lcd_init_cmd_t *init_cmds = NULL;
    uint16_t init_cmds_size = 0;
    if (nv3007->init_cmds)
    {
        init_cmds = nv3007->init_cmds;
        init_cmds_size = nv3007->init_cmds_size;
    }
    else
    {
        init_cmds = vendor_specific_init_default;
        init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(nv3007_lcd_init_cmd_t);
    }

    bool is_cmd_overwritten = false;
    for (int i = 0; i < init_cmds_size; i++)
    {
        // Check if the command has been used or conflicts with the internal
        switch (init_cmds[i].cmd)
        {
        case LCD_CMD_MADCTL: // 控制方向
            is_cmd_overwritten = true;
            nv3007->madctl_val = ((uint8_t *)init_cmds[i].data)[0];
            break;
        case LCD_CMD_COLMOD: // 颜色格式
            is_cmd_overwritten = true;
            nv3007->colmod_val = ((uint8_t *)init_cmds[i].data)[0];
            break;
        default:
            is_cmd_overwritten = false;
            break;
        }

        if (is_cmd_overwritten)
        {
            ESP_LOGW(TAG, "The %02Xh command has been used and will be overwritten by external initialization sequence", init_cmds[i].cmd);
        }

        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
    }
    ESP_LOGD(TAG, "send init commands success");

    return ESP_OK;
}

static esp_err_t panel_nv3007_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    nv3007_panel_t *nv3007 = __containerof(panel, nv3007_panel_t, base);
    assert((x_start < x_end) && (y_start < y_end) && "start position must be smaller than end position");
    esp_lcd_panel_io_handle_t io = nv3007->io;

    x_start += nv3007->x_gap;
    x_end += nv3007->x_gap;
    y_start += nv3007->y_gap;
    y_end += nv3007->y_gap;

    if (nv3007->madctl_val == 0x00)
    {
        x_start += 12;
        x_end += 12;
    }
    else
    {
        y_start += 14;
        y_end += 14;
    }
    

    // define an area of frame memory where MCU can access
    esp_lcd_panel_io_tx_param(io, LCD_CMD_CASET, (uint8_t[]) {
        (x_start >> 8) & 0xFF,
        x_start & 0xFF,
        ((x_end - 1) >> 8) & 0xFF,
        (x_end - 1) & 0xFF,
    }, 4);
    esp_lcd_panel_io_tx_param(io, LCD_CMD_RASET, (uint8_t[]) {
        (y_start >> 8) & 0xFF,
        y_start & 0xFF,
        ((y_end - 1) >> 8) & 0xFF,
        (y_end - 1) & 0xFF,
    }, 4);
    // transfer frame buffer
    size_t len = (x_end - x_start) * (y_end - y_start) * nv3007->fb_bits_per_pixel / 8;
    esp_lcd_panel_io_tx_color(io, LCD_CMD_RAMWR, color_data, len);

    return ESP_OK;
}

static esp_err_t panel_nv3007_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    nv3007_panel_t *nv3007 = __containerof(panel, nv3007_panel_t, base);
    esp_lcd_panel_io_handle_t io = nv3007->io;
    int command = 0;
    if (invert_color_data) {
        command = LCD_CMD_INVON;
    } else {
        command = LCD_CMD_INVOFF;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_nv3007_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    nv3007_panel_t *nv3007 = __containerof(panel, nv3007_panel_t, base);
    esp_lcd_panel_io_handle_t io = nv3007->io;
    if (mirror_x) {
        nv3007->madctl_val |= LCD_CMD_MX_BIT;
    } else {
        nv3007->madctl_val &= ~LCD_CMD_MX_BIT;
    }
    if (mirror_y) {
        nv3007->madctl_val |= LCD_CMD_MY_BIT;
    } else {
        nv3007->madctl_val &= ~LCD_CMD_MY_BIT;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]) {
        nv3007->madctl_val
    }, 1), TAG, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_nv3007_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    ESP_LOGI(TAG, "swap_xy: %d", swap_axes);
    nv3007_panel_t *nv3007 = __containerof(panel, nv3007_panel_t, base);
    esp_lcd_panel_io_handle_t io = nv3007->io;
    if (swap_axes) {
        nv3007->madctl_val = 0xA0;
    } else {
        nv3007->madctl_val = 0x00;
    }
    esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]) {
        nv3007->madctl_val
    }, 1);
    return ESP_OK;
}

static esp_err_t panel_nv3007_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    nv3007_panel_t *nv3007 = __containerof(panel, nv3007_panel_t, base);
    nv3007->x_gap = x_gap;
    nv3007->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t panel_nv3007_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    nv3007_panel_t *nv3007 = __containerof(panel, nv3007_panel_t, base);
    esp_lcd_panel_io_handle_t io = nv3007->io;
    int command = 0;

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    on_off = !on_off;
#endif

    if (on_off) {
        command = LCD_CMD_DISPON;
    } else {
        command = LCD_CMD_DISPOFF;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");
    return ESP_OK;
}

esp_err_t esp_lcd_new_panel_nv3007(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)
{
    esp_err_t ret = ESP_OK;
    nv3007_panel_t *nv3007 = NULL;
    gpio_config_t io_conf = {0};

    ESP_GOTO_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    nv3007 = (nv3007_panel_t *)calloc(1, sizeof(nv3007_panel_t));
    ESP_GOTO_ON_FALSE(nv3007, ESP_ERR_NO_MEM, err, TAG, "no mem for nv3007 panel");

    if (panel_dev_config->reset_gpio_num >= 0)
    {
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num;
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    switch (panel_dev_config->color_space)
    {
    case ESP_LCD_COLOR_SPACE_RGB:
        nv3007->madctl_val = 0;
        break;
    case ESP_LCD_COLOR_SPACE_BGR:
        nv3007->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported color space");
        break;
    }
#else
    switch (panel_dev_config->rgb_endian)
    {
    case LCD_RGB_ENDIAN_RGB:
        nv3007->madctl_val = 0;
        break;
    case LCD_RGB_ENDIAN_BGR:
        nv3007->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported rgb endian");
        break;
    }
#endif

    switch (panel_dev_config->bits_per_pixel)
    {
    case 16: // RGB565
        nv3007->colmod_val = 0x5;
        nv3007->fb_bits_per_pixel = 16;
        break;
    case 18: // RGB666
        nv3007->colmod_val = 0x6;
        // each color component (R/G/B) should occupy the 6 high bits of a byte, which means 3 full bytes are required for a pixel
        nv3007->fb_bits_per_pixel = 24;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
        break;
    }

    nv3007->io = io;
    nv3007->reset_gpio_num = panel_dev_config->reset_gpio_num;
    nv3007->reset_level = panel_dev_config->flags.reset_active_high;
    if (panel_dev_config->vendor_config)
    {
        nv3007->init_cmds = ((nv3007_vendor_config_t *)panel_dev_config->vendor_config)->init_cmds;
        nv3007->init_cmds_size = ((nv3007_vendor_config_t *)panel_dev_config->vendor_config)->init_cmds_size;
    }
    nv3007->base.del = panel_nv3007_del;
    nv3007->base.reset = panel_nv3007_reset;
    nv3007->base.init = panel_nv3007_init;
    nv3007->base.draw_bitmap = panel_nv3007_draw_bitmap;
    nv3007->base.invert_color = panel_nv3007_invert_color;
    nv3007->base.set_gap = panel_nv3007_set_gap;
    nv3007->base.mirror = panel_nv3007_mirror;
    nv3007->base.swap_xy = panel_nv3007_swap_xy;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    nv3007->base.disp_off = panel_nv3007_disp_on_off;
#else
    nv3007->base.disp_on_off = panel_nv3007_disp_on_off;
#endif
    *ret_panel = &(nv3007->base);
    ESP_LOGD(TAG, "new nv3007 panel @%p", nv3007);

    // ESP_LOGI(TAG, "LCD panel create success, version: %d.%d.%d", ESP_LCD_NV3007_VER_MAJOR, ESP_LCD_NV3007_VER_MINOR,
    //          ESP_LCD_NV3007_VER_PATCH);

    ESP_LOGI(TAG, "LCD panel create success, version: %d.%d.%d", 0, 0,
        0);

    return ESP_OK;

err:
    if (nv3007)
    {
        if (panel_dev_config->reset_gpio_num >= 0)
        {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(nv3007);
    }
    return ret;
}
