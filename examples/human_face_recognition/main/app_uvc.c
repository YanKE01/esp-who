#include "app_uvc.h"

static esp_lcd_panel_handle_t display_panel = NULL;

esp_err_t app_uvc_display_draw_frame(size_t h_res, size_t v_res, const uint8_t *frame_data)
{
    return esp_lcd_panel_draw_bitmap(display_panel, 0, 0, h_res, v_res, frame_data);
}

esp_err_t app_uvc_display_main(size_t h_res, size_t v_res)
{
    usb_display_vendor_config_t vendor_config = DEFAULT_USB_DISPLAY_VENDOR_CONFIG(h_res, v_res, 16, display_panel); // 565
    ESP_ERROR_CHECK(esp_lcd_new_panel_usb_display(&vendor_config, &display_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(display_panel));
    return ESP_OK;
}