#include "who_cam_lcd.hpp"
#include "display_func_manager.hpp"

static const char *TAG = "WhoCamLCD";

namespace who {
namespace app {
EventGroupHandle_t WhoCamLCD::s_event_group = xEventGroupCreate();
EventBits_t WhoCamLCD::s_task_bits = 0;

typedef struct {
    int64_t start;
    int64_t acc;
    char str1[15];
    char str2[15];
} PerfCounter;
static PerfCounter perf_counters[1] = {0};

static void perfmon_start(int ctr, const char *fmt1, const char *fmt2, ...)
{
    va_list args;
    va_start(args, fmt2);
    vsnprintf(perf_counters[ctr].str1, sizeof(perf_counters[ctr].str1), fmt1, args);
    vsnprintf(perf_counters[ctr].str2, sizeof(perf_counters[ctr].str2), fmt2, args);
    va_end(args);

    perf_counters[ctr].start = esp_timer_get_time();
}

static void perfmon_end(int ctr, int count)
{
    int64_t time_diff = esp_timer_get_time() - perf_counters[ctr].start;
    float time_in_sec = (float)time_diff / 1000000;
    float frequency = count / time_in_sec;

    printf("Perf ctr[%d], [%15s][%15s]: %.2f FPS (%.2f ms per operation)\n",
           ctr, perf_counters[ctr].str1, perf_counters[ctr].str2, frequency, time_in_sec * 1000 / count);
}

void WhoCamLCD::task(void *args)
{
    WhoCamLCD *self = (WhoCamLCD *)args;
    auto display_func_manager = DisplayFuncManager::get_instance();

#if CONFIG_IDF_TARGET_ESP32P4
    int n = self->m_cam->m_fb_count - 2;
#elif CONFIG_IDF_TARGET_ESP32S3
    int n = self->m_cam->m_fb_count - 1;
#endif
    for (int i = 0; i < n; i++) {
        self->m_cam->cam_fb_get();
    }
    while (true) {
        if (s_task_bits) {
            xEventGroupSetBits(s_event_group, s_task_bits);
        }
        self->m_cam->cam_fb_get();
        who::cam::cam_fb_t *fb = self->m_cam->cam_fb_peek(false);
        display_func_manager->display(fb);
        self->m_cam->cam_fb_return();


        static int count = 0;
        if (count % 10 == 0) {
            perfmon_start(0, "PFS", "camera");
        } else if (count % 10 == 9) {
            perfmon_end(0, 10);
        }
        count++;
    }
}

void WhoCamLCD::run()
{
    if (xTaskCreatePinnedToCore(task, "CamLCD", 4096, this, 2, nullptr, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create CamLCD task.\n");
    };
}

} // namespace app
} // namespace who
