#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_usb_display.h"

esp_err_t app_uvc_display_draw_frame(size_t h_res, size_t v_res, const uint8_t *frame_data);
esp_err_t app_uvc_display_main(size_t h_res, size_t v_res);

#ifdef __cplusplus
}
#endif