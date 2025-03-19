#include "helper.hpp"
#include "cam.hpp"

namespace who {
namespace app {
using namespace who::lcd;

#if BSP_CONFIG_NO_GRAPHIC_LIB
void draw_detect_results(who::cam::cam_fb_t *fb,
                         const std::list<dl::detect::result_t> &detect_res,
                         const std::vector<uint8_t> &color)
{
#if CONFIG_IDF_TARGET_ESP32P4
    uint32_t caps = DL_IMAGE_CAP_RGB565_BIG_ENDIAN;
#else
    uint32_t caps = 0;
#endif
    dl::image::img_t img = who::cam::fb2img(fb);
    for (const auto &res : detect_res) {
        dl::image::draw_hollow_rectangle(img, res.box[0], res.box[1], res.box[2], res.box[3], color, 2, caps);
        if (!res.keypoint.empty()) {
            assert(res.keypoint.size() == 10);
            for (int i = 0; i < 5; i++) {
                dl::image::draw_point(img, res.keypoint[2 * i], res.keypoint[2 * i + 1], color, 5, caps);
            }
        }
    }
    LCD::set_cam_fb(fb);
}
#else

#define WIDTH  1280
#define HEIGHT 720

void draw_rectangle_rgb(uint16_t *buffer, int width, int height, int x1, int y1, int x2, int y2, int x_offset, int y_offset, uint8_t r, uint8_t g, uint8_t b, int thickness)
{
    // Apply offset to the coordinates
    x1 += x_offset;
    x2 += x_offset;
    y1 += y_offset;
    y2 += y_offset;

    // Clip coordinates to the buffer dimensions
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= width) x2 = width - 1;
    if (y2 >= height) y2 = height - 1;

    // Convert RGB888 to RGB565
    uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

    // Draw the top and bottom horizontal lines with thickness
    for (int t = 0; t < thickness; ++t) {
        for (int x = x1; x <= x2; ++x) {
            if (y1 + t >= 0 && y1 + t < height && x >= 0 && x < width) {
                buffer[(y1 + t) * width + x] = color;
            }
            if (y2 - t >= 0 && y2 - t < height && x >= 0 && x < width) {
                buffer[(y2 - t) * width + x] = color;
            }
        }
    }

    // Draw the left and right vertical lines with thickness
    for (int t = 0; t < thickness; ++t) {
        for (int y = y1; y <= y2; ++y) {
            if (x1 + t >= 0 && x1 + t < width && y >= 0 && y < height) {
                buffer[y * width + (x1 + t)] = color;
            }
            if (x2 - t >= 0 && x2 - t < width && y >= 0 && y < height) {
                buffer[y * width + (x2 - t)] = color;
            }
        }
    }
}


static void draw_large_green_point(uint16_t *buffer, int x, int y) {
    uint16_t green = 0x07E0; 
    
    for (int dx = -3; dx <= 3; ++dx) {
        for (int dy = -3; dy <= 3; ++dy) {
            int nx = x + dx;
            int ny = y + dy;

            if (nx >= 0 && nx < WIDTH && ny >= 0 && ny < HEIGHT) {
                buffer[ny * WIDTH + nx] = green;
            }
        }
    }
}

void draw_green_points(uint16_t *buffer, const std::vector<int> &landmarks) 
{
    for (int i = 0; i < 5; i++) {
        int x = landmarks[2 * i];     
        int y = landmarks[2 * i + 1]; 

        draw_large_green_point(buffer, x, y);
    }
}


// void draw_pixel_rgb(uint16_t *buffer, int width, int height, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
//     // Convert RGB888 to RGB565
//     uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

//     // Check if the coordinates are within the buffer dimensions
//     if (x >= 0 && x < width && y >= 0 && y < height) {
//         // Set the pixel color in the buffer
//         buffer[y * width + x] = color;
//     }
// }

void draw_pixel_rgb(uint16_t *buffer, int width, int height, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint16_t thickness) {
    // Convert RGB888 to RGB565
    uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

    // Calculate half size for the pixel block based on thickness
    int half_thickness = thickness / 2;

    // Draw a block of pixels centered at (x, y)
    for (int dy = -half_thickness; dy <= half_thickness; ++dy) {
        for (int dx = -half_thickness; dx <= half_thickness; ++dx) {
            int current_x = x + dx;
            int current_y = y + dy;

            if (current_x >= 0 && current_x < width && current_y >= 0 && current_y < height) {
                buffer[current_y * width + current_x] = color;
            }
        }
    }
}


void draw_detect_results(who::cam::cam_fb_t *fb,
                         const std::list<dl::detect::result_t> &detect_res,
                         const std::vector<uint8_t> &color)
{
    // bsp_display_lock(0);
    uint16_t *rgb_buf = reinterpret_cast<uint16_t *>(fb->buf);
    for (const auto &res : detect_res) {
        draw_rectangle_rgb(rgb_buf, fb->width, fb->height,
                                res.box[0], res.box[1], res.box[2], res.box[3],
                                0, 0, 255, 0, 0, 3);

        if (!res.keypoint.empty()) {
            assert(res.keypoint.size() == 10);
            for (int i = 0; i < 5; i++) {
                int x = res.keypoint[2 * i];
                int y = res.keypoint[2 * i + 1];
                draw_pixel_rgb(rgb_buf, fb->width, fb->height, x, y, 255, 0, 0,5);
            }
        }
    }

    LCD::set_cam_fb(fb);
// #if CONFIG_IDF_TARGET_ESP32P4
//     lv_color_t c = lv_color_make(color[0], color[1], color[2]);
// #else
//     lv_color_t c = cvt_little_endian_color(lv_color_make(color[0], color[1], color[2]));
// #endif

//     lv_draw_rect_dsc_t rect_dsc;
//     lv_draw_rect_dsc_init(&rect_dsc);
//     rect_dsc.bg_opa = LV_OPA_TRANSP;
//     rect_dsc.border_width = 2;
//     rect_dsc.border_color = c;

//     lv_draw_arc_dsc_t arc_dsc;
//     lv_draw_arc_dsc_init(&arc_dsc);
//     arc_dsc.color = c;
//     arc_dsc.width = 5;
//     arc_dsc.radius = 5;
//     arc_dsc.start_angle = 0;
//     arc_dsc.end_angle = 360;

//     lv_layer_t layer;
//     lv_canvas_init_layer(LCD::s_canvas, &layer);
//     lv_area_t coords_rect;
//     for (const auto &res : detect_res) {
//         coords_rect = {res.box[0], res.box[1], res.box[2], res.box[3]};
//         lv_draw_rect(&layer, &rect_dsc, &coords_rect);
//         if (!res.keypoint.empty()) {
//             assert(res.keypoint.size() == 10);
//             for (int i = 0; i < 5; i++) {
//                 arc_dsc.center.x = res.keypoint[2 * i];
//                 arc_dsc.center.y = res.keypoint[2 * i + 1];
//                 lv_draw_arc(&layer, &arc_dsc);
//             }
//         }
//     }
//     lv_canvas_finish_layer(LCD::s_canvas, &layer);
//     bsp_display_unlock();
}

lv_obj_t *create_lvgl_btn(const char *text, const lv_font_t *font)
{
    lv_obj_t *btn = lv_button_create(lv_scr_act());
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_text_font(&style, font);
    lv_obj_add_style(btn, &style, 0);
    lv_obj_center(label);
    return btn;
}

lv_obj_t *create_lvgl_label(const char *text, const lv_font_t *font, const std::vector<uint8_t> &color)
{
#if CONFIG_IDF_TARGET_ESP32P4
    lv_color_t c = lv_color_make(color[0], color[1], color[2]);
#else
    lv_color_t c = cvt_little_endian_color(lv_color_make(color[0], color[1], color[2]));
#endif
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, text);
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_text_font(&style, font);
    lv_style_set_text_color(&style, c);
    lv_obj_add_style(label, &style, 0);
    return label;
}
#endif
} // namespace app
} // namespace who
