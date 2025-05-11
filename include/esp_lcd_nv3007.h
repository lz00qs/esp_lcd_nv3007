#pragma once

#include "esp_lcd_panel_vendor.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define NV3007_PANEL_BUS_SPI_CONFIG(sclk, mosi, max_trans_sz) \
    {                                                         \
        .sclk_io_num = sclk,                                  \
        .mosi_io_num = mosi,                                  \
        .miso_io_num = -1,                                    \
        .quadhd_io_num = -1,                                  \
        .quadwp_io_num = -1,                                  \
        .max_transfer_sz = max_trans_sz,                      \
    }

#define NV3007_PANEL_IO_SPI_CONFIG(cs, dc, callback, callback_ctx) \
    {                                                              \
        .cs_gpio_num = cs,                                         \
        .dc_gpio_num = dc,                                         \
        .spi_mode = 0,                                             \
        .pclk_hz = 40 * 1000 * 1000,                               \
        .trans_queue_depth = 10,                                   \
        .on_color_trans_done = callback,                           \
        .user_ctx = callback_ctx,                                  \
        .lcd_cmd_bits = 8,                                         \
        .lcd_param_bits = 8,                                       \
    }

    typedef struct
    {
        int cmd;               /*<! The specific LCD command */
        const void *data;      /*<! Buffer that holds the command specific data */
        size_t data_bytes;     /*<! Size of `data` in memory, in bytes */
        unsigned int delay_ms; /*<! Delay in milliseconds after this command */
    } nv3007_lcd_init_cmd_t;

    /**
     * @brief LCD panel vendor configuration.
     *
     * @note  This structure needs to be passed to the `vendor_config` field in `esp_lcd_panel_dev_config_t`.
     *
     */
    typedef struct
    {
        const nv3007_lcd_init_cmd_t *init_cmds; /*!< Pointer to initialization commands array. Set to NULL if using default commands.
                                                 *   The array should be declared as `static const` and positioned outside the function.
                                                 *   Please refer to `vendor_specific_init_default` in source file.
                                                 */
        uint16_t init_cmds_size;                /*<! Number of commands in above array */
    } nv3007_vendor_config_t;

    esp_err_t esp_lcd_new_panel_nv3007(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel);

#ifdef __cplusplus
}
#endif