#include "who_recognition.hpp"
#include "display_func_manager.hpp"
#include "helper.hpp"
#include "driver/jpeg_encode.h"

#if CONFIG_IDF_TARGET_ESP32P4
#define WHO_REC_RES_SHOW_N_FRAMES (60)
#elif CONFIG_IDF_TARGET_ESP32S3
#define WHO_REC_RES_SHOW_N_FRAMES (30)
#endif

static const char *TAG = "who_recognition";
LV_FONT_DECLARE(montserrat_bold_26);

namespace who {
namespace app {

static bool detect_enable_flag = true;

using namespace who::lcd;
TaskHandle_t WhoHumanFaceRecognition::s_task_handle = nullptr;

void WhoHumanFaceRecognition::event_handle_task(void *args)
{
    WhoHumanFaceRecognition *self = (WhoHumanFaceRecognition *)args;
    while (true) {
        uint32_t event;
        xTaskNotifyWait(0, 0xffffffff, &event, portMAX_DELAY);
        if (event & static_cast<uint32_t>(fr_event_t::RECOGNIZE)) {
            xSemaphoreTake(self->m_status_mutex, portMAX_DELAY);
            self->m_status = fr_status_t::RECOGNIZE;
            xSemaphoreGive(self->m_status_mutex);
        }
        if (event & static_cast<uint32_t>(fr_event_t::ENROLL)) {
            xSemaphoreTake(self->m_status_mutex, portMAX_DELAY);
            self->m_status = fr_status_t::ENROLL;
            xSemaphoreGive(self->m_status_mutex);
        }
        if (event & static_cast<uint32_t>(fr_event_t::DELETE)) {
            xSemaphoreTake(self->m_status_mutex, portMAX_DELAY);
            self->m_status = fr_status_t::DELETE;
            xSemaphoreGive(self->m_status_mutex);
        }
    }
}

void WhoHumanFaceRecognition::recognition_task(void *args)
{
    WhoHumanFaceRecognition *self = (WhoHumanFaceRecognition *)args;
    // self->create_btns();
    // self->create_label();
    struct timeval timestamp;
    fr_status_t status;
    int64_t start = esp_timer_get_time();
    int64_t task_wt_interval = 1000000;
    esp_err_t res;
    while (true) {
        xSemaphoreTake(self->m_status_mutex, portMAX_DELAY);
        status = self->m_status;
        xSemaphoreGive(self->m_status_mutex);
        switch (status) {
        case fr_status_t::RECOGNIZE: {
            auto *fb = self->m_cam->cam_fb_peek();
            timestamp = fb->timestamp;

            res = jpeg_encoder_process(self->jpeg_handle, &self->jpeg_enc_config, (uint8_t *)fb->buf, fb->len, self->jpeg_out_buf, self->jpeg_enc_output_buf_alloced_size, &self->jpeg_encoded_size);

            if (res != ESP_OK) {
                ESP_LOGE(TAG, "jpeg encode failed");
            }

            dl::image::jpeg_img_t jpeg_img = {
                .data = self->jpeg_out_buf,
                .width = 640,
                .height = 480,
                .data_size = self->jpeg_encoded_size
            };

            dl::image::img_t dl_img;
            dl_img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888;
            res  = sw_decode_jpeg(jpeg_img, dl_img, true);

            if (res != ESP_OK) {
                ESP_LOGE(TAG, "jpeg decode failed");
            }

            auto &det_res = self->m_detect->run(dl_img);
            xSemaphoreTake(self->m_det_res_mutex, portMAX_DELAY);
            self->m_det_results.push({det_res, timestamp});
            xSemaphoreGive(self->m_det_res_mutex);
            auto rec_res = self->m_recognizer->recognize(dl_img, det_res);
            heap_caps_free(dl_img.data);

            char *text = new char[64];
            if (rec_res.empty()) {
                strcpy(text, "who?");
            } else {
                snprintf(text, 64, "id: %d, sim: %.2f", rec_res[0].id, rec_res[0].similarity);
            }
            printf("recognize: %s\n", text);
            xSemaphoreTake(self->m_rec_res_mutex, portMAX_DELAY);
            self->m_rec_results.emplace_back(text);
            xSemaphoreGive(self->m_rec_res_mutex);
            xSemaphoreTake(self->m_status_mutex, portMAX_DELAY);
            self->m_status = fr_status_t::DETECT;
            xSemaphoreGive(self->m_status_mutex);
            break;
        }
        case fr_status_t::ENROLL: {
            auto *fb = self->m_cam->cam_fb_peek();
            timestamp = fb->timestamp;

            res = jpeg_encoder_process(self->jpeg_handle, &self->jpeg_enc_config, (uint8_t *)fb->buf, fb->len, self->jpeg_out_buf, self->jpeg_enc_output_buf_alloced_size, &self->jpeg_encoded_size);

            if (res != ESP_OK) {
                ESP_LOGE(TAG, "jpeg encode failed");
            }

            dl::image::jpeg_img_t jpeg_img = {
                .data = self->jpeg_out_buf,
                .width = 640,
                .height = 480,
                .data_size = self->jpeg_encoded_size
            };

            dl::image::img_t dl_img;
            dl_img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888;
            res  = sw_decode_jpeg(jpeg_img, dl_img, true);

            if (res != ESP_OK) {
                ESP_LOGE(TAG, "jpeg decode failed");
            }


            // auto img = who::cam::fb2img(fb);
            auto &det_res = self->m_detect->run(dl_img);
            xSemaphoreTake(self->m_det_res_mutex, portMAX_DELAY);
            self->m_det_results.push({det_res, timestamp});
            xSemaphoreGive(self->m_det_res_mutex);
            esp_err_t ret = self->m_recognizer->enroll(dl_img, det_res);
            char *text = new char[64];
            if (ret == ESP_FAIL) {
                strcpy(text, "Failed to enroll.");
            } else {
                snprintf(text, 64, "id: %d enrolled.", self->m_recognizer->get_num_feats());
            }
            printf("enroll: %s\n", text);
            xSemaphoreTake(self->m_rec_res_mutex, portMAX_DELAY);
            self->m_rec_results.emplace_back(text);
            xSemaphoreGive(self->m_rec_res_mutex);
            xSemaphoreTake(self->m_status_mutex, portMAX_DELAY);
            self->m_status = fr_status_t::DETECT;
            xSemaphoreGive(self->m_status_mutex);
            break;
        }
        case fr_status_t::DELETE: {
            esp_err_t ret = self->m_recognizer->delete_last_feat();
            char *text = new char[64];
            if (ret == ESP_FAIL) {
                strcpy(text, "Failed to delete.");
            } else {
                snprintf(text, 64, "id: %d deleted.", self->m_recognizer->get_num_feats() + 1);
            }
            xSemaphoreTake(self->m_rec_res_mutex, portMAX_DELAY);
            self->m_rec_results.emplace_back(text);
            xSemaphoreGive(self->m_rec_res_mutex);
            xSemaphoreTake(self->m_status_mutex, portMAX_DELAY);
            self->m_status = fr_status_t::DETECT;
            xSemaphoreGive(self->m_status_mutex);
            break;
        }
        case fr_status_t::DETECT: {
            auto *fb = self->m_cam->cam_fb_peek();
            timestamp = fb->timestamp;

            res = jpeg_encoder_process(self->jpeg_handle, &self->jpeg_enc_config, (uint8_t *)fb->buf, fb->len, self->jpeg_out_buf, self->jpeg_enc_output_buf_alloced_size, &self->jpeg_encoded_size);

            if (res != ESP_OK) {
                ESP_LOGE(TAG, "jpeg encode failed");
            }

            dl::image::jpeg_img_t jpeg_img = {
                .data = self->jpeg_out_buf,
                .width = 640,
                .height = 480,
                .data_size = self->jpeg_encoded_size
            };

            dl::image::img_t dl_img;
            dl_img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888;
            res  = sw_decode_jpeg(jpeg_img, dl_img, true);

            if (res != ESP_OK) {
                ESP_LOGE(TAG, "jpeg decode failed");
            }

            auto &det_res = self->m_detect->run(dl_img);
            if (detect_enable_flag == false) {
                det_res = {};
            }

            xSemaphoreTake(self->m_det_res_mutex, portMAX_DELAY);
            self->m_det_results.push({det_res, timestamp});
            xSemaphoreGive(self->m_det_res_mutex);
            heap_caps_free(dl_img.data);

            break;

        }
        }
    }
    int64_t end = esp_timer_get_time();
    if (end - start >= task_wt_interval) {
        vTaskDelay(pdMS_TO_TICKS(10));
        start = esp_timer_get_time();
    }

}

void WhoHumanFaceRecognition::lvgl_btn_event_handler(lv_event_t *e)
{
    fr_event_t fr_event = (fr_event_t)(reinterpret_cast<int>(lv_event_get_user_data(e)));
    btn_event_handler(fr_event);
}

void WhoHumanFaceRecognition::iot_btn_event_handler(void *button_handle, void *usr_data)
{
    fr_event_t fr_event = (fr_event_t)(reinterpret_cast<int>(usr_data));
    btn_event_handler(fr_event);
}

inline void WhoHumanFaceRecognition::btn_event_handler(fr_event_t fr_event)
{
    switch (fr_event) {
    case fr_event_t::RECOGNIZE:
        xTaskNotify(s_task_handle, (uint32_t)fr_event_t::RECOGNIZE, eSetBits);
        break;
    case fr_event_t::ENROLL:
        xTaskNotify(s_task_handle, (uint32_t)fr_event_t::ENROLL, eSetBits);
        break;
    case fr_event_t::DELETE:
        xTaskNotify(s_task_handle, (uint32_t)fr_event_t::DELETE, eSetBits);
        break;
    }
}

void WhoHumanFaceRecognition::create_btns()
{
#if CONFIG_IDF_TARGET_ESP32P4
    bsp_display_lock(0);
    lv_obj_t *btn_recognize = create_lvgl_btn("recognize", &montserrat_bold_26);
    lv_obj_t *btn_enroll = create_lvgl_btn("enroll", &montserrat_bold_26);
    lv_obj_t *btn_delete = create_lvgl_btn("delete", &montserrat_bold_26);
    lv_obj_add_event_cb(btn_recognize, lvgl_btn_event_handler, LV_EVENT_CLICKED, (void *)fr_event_t::RECOGNIZE);
    lv_obj_add_event_cb(btn_enroll, lvgl_btn_event_handler, LV_EVENT_CLICKED, (void *)fr_event_t::ENROLL);
    lv_obj_add_event_cb(btn_delete, lvgl_btn_event_handler, LV_EVENT_CLICKED, (void *)fr_event_t::DELETE);
    lv_obj_update_layout(btn_recognize);
    lv_obj_update_layout(btn_enroll);
    lv_obj_update_layout(btn_delete);
    int32_t w = lv_obj_get_width(btn_recognize);
    w = std::max(w, lv_obj_get_width(btn_enroll));
    w = std::max(w, lv_obj_get_width(btn_delete));
    int32_t h = lv_obj_get_height(btn_recognize);
    lv_obj_set_size(btn_recognize, w, h);
    lv_obj_set_size(btn_enroll, w, h);
    lv_obj_set_size(btn_delete, w, h);
    int32_t pad = h / 2;
    lv_obj_align(btn_recognize, LV_ALIGN_TOP_RIGHT, -pad, pad);
    lv_obj_align(btn_enroll, LV_ALIGN_TOP_RIGHT, -pad, pad + h + pad);
    lv_obj_align(btn_delete, LV_ALIGN_TOP_RIGHT, -pad, pad + 2 * (h + pad));
    bsp_display_unlock();
#elif CONFIG_IDF_TARGET_ESP32S3
    button_handle_t btns[BSP_BUTTON_NUM];
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    // play  recognize
    ESP_ERROR_CHECK(
        iot_button_register_cb(btns[1], BUTTON_SINGLE_CLICK, iot_btn_event_handler, (void *)fr_event_t::RECOGNIZE));
    // up    enroll
    ESP_ERROR_CHECK(
        iot_button_register_cb(btns[3], BUTTON_SINGLE_CLICK, iot_btn_event_handler, (void *)fr_event_t::ENROLL));
    // down  delete
    ESP_ERROR_CHECK(
        iot_button_register_cb(btns[2], BUTTON_SINGLE_CLICK, iot_btn_event_handler, (void *)fr_event_t::DELETE));
#endif
}

void WhoHumanFaceRecognition::create_label()
{
    bsp_display_lock(0);
    m_label = create_lvgl_label("", &montserrat_bold_26);
    const lv_font_t *font = lv_obj_get_style_text_font(m_label, LV_PART_MAIN);
    lv_obj_align(m_label, LV_ALIGN_TOP_MID, 0, font->line_height);
    bsp_display_unlock();
}

void WhoHumanFaceRecognition::display(who::cam::cam_fb_t *fb)
{
    xSemaphoreTake(m_det_res_mutex, portMAX_DELAY);
    // Try to sync camera frame and result, skip the future result.
    struct timeval t1 = fb->timestamp;
    bool display = false;
    det_result_t det_result;
    while (!m_det_results.empty()) {
        det_result = m_det_results.front();
        if (!compare_timestamp(t1, det_result.timestamp)) {
            m_det_results.pop();
            display = true;
        } else {
            break;
        }
    }
    xSemaphoreGive(m_det_res_mutex);
    if (display) {
        draw_detect_results(fb, det_result.det_res);
    }
}

void WhoHumanFaceRecognition::run()
{
    jpeg_enc_config.src_type = JPEG_ENCODE_IN_FORMAT_YUV422;
    jpeg_enc_config.image_quality = 80;
    jpeg_enc_config.width = 640;
    jpeg_enc_config.height = 480;
    jpeg_enc_config.sub_sample = JPEG_DOWN_SAMPLING_YUV422;
    jpeg_enc_input_src_size = jpeg_enc_config.width * jpeg_enc_config.height * 2;

    jpeg_encode_engine_cfg_t encode_eng_cfg = {
        .timeout_ms = 500,
    };
    ESP_ERROR_CHECK(jpeg_new_encoder_engine(&encode_eng_cfg, &jpeg_handle));

    jpeg_encode_memory_alloc_cfg_t jpeg_enc_output_mem_cfg = {
        .buffer_direction = (jpeg_enc_buffer_alloc_direction_t)JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };

    jpeg_out_buf = (uint8_t *)jpeg_alloc_encoder_mem(jpeg_enc_input_src_size / 2, &jpeg_enc_output_mem_cfg, &jpeg_enc_output_buf_alloced_size);
    if (!jpeg_out_buf) {
        ESP_LOGE(TAG, "failed to alloc jpeg output buf");
        return;
    }

    ESP_LOGI(TAG, "jpeg encoder init success");

    auto display_func_manager = DisplayFuncManager::get_instance();
    display_func_manager->register_display_func(
        "WhoRec", std::bind(&WhoHumanFaceRecognition::display, this, std::placeholders::_1));
    if (xTaskCreatePinnedToCore(event_handle_task, "WhoRecEvent", 2560, this, 2, &s_task_handle, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WhoRecog_event task.\n");
    }
    if (xTaskCreatePinnedToCore(recognition_task, "WhoRec", 3584, this, 2, nullptr, 1) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WhoRecog task.\n");
    }
}

void WhoHumanFaceRecognition::enroll()
{
    xTaskNotify(s_task_handle, (uint32_t)fr_event_t::ENROLL, eSetBits);
}

void WhoHumanFaceRecognition::recognize()
{
    xTaskNotify(s_task_handle, (uint32_t)fr_event_t::RECOGNIZE, eSetBits);
}

int WhoHumanFaceRecognition::get_all_registered_id()
{
    return m_recognizer->get_num_feats();
}

void WhoHumanFaceRecognition::detect_enable(bool enable)
{
    detect_enable_flag = enable;
}

esp_err_t WhoHumanFaceRecognition::delete_rec_result(int target_id)
{
    return m_recognizer->delete_feat(target_id);
}

} // namespace app
} // namespace who
