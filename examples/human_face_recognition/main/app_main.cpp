#include "spiflash_fatfs.hpp"
#include "who_cam_lcd.hpp"
#include "who_recognition.hpp"
#include "esp_lcd_usb_display.h"
#include "app_uvc.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"

using namespace who::app;
using namespace who::cam;
using namespace dl::detect;

static const char *TAG = "HumanFaceRecognition";

esp_console_repl_t *repl = NULL;
esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
WhoHumanFaceRecognition *who_recognition = nullptr;
P4Cam *cam = nullptr;

static struct {
    struct arg_str *id;
    struct arg_end *end;
} at_delete_args;

static struct {
    struct arg_str *exposure;
    struct arg_end *end;
} at_exposure_args;

bool is_all_digits(const char *str)
{
    if (str == NULL || *str == '\0') {
        return false;
    }
    while (*str) {
        if (!isdigit((unsigned char) * str)) {
            return false;
        }
        str++;
    }
    return true;
}

int at_enroll_func(int argc, char **argv)
{
    who_recognition->enroll();
    return 0;
}

int at_delete_func(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&at_delete_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, at_delete_args.end, argv[0]);
        return 1;
    }

    const char *param_value = at_delete_args.id->sval[0];  // 拿到参数

    if (!is_all_digits(param_value)) {
        printf("Error: Parameter must be a numeric ID, but got '%s'\n", param_value);
        return 1;  // 返回失败
    }

    int target_id = atoi(param_value);
    // printf("Converted numeric ID: %d\n", target_id);

    esp_err_t ret = who_recognition->delete_rec_result(target_id);

    if (ret == ESP_OK) {
        printf("ID %d deleted successfully.\n", target_id);
        return 0;
    }

    return 1;
}

int at_recognize_start_func(int argc, char **argv)
{
    who_recognition->is_recognize = true;
    return 0;
}

int at_recognize_stop_func(int argc, char **argv)
{
    who_recognition->is_recognize = false;
    return 0;
}

int at_reset_func(int argc, char **argv)
{
    esp_restart();
}

int at_get_all_registered_id_func(int argc, char **argv)
{
    int id = who_recognition->get_all_registered_id();
    printf("All registered Num: %d\n", id);
    return 0;
}

int at_face_start_func(int argc, char **argv)
{
    who_recognition->detect_enable(true);
    return 0;
}

int at_face_stop_func(int argc, char **argv)
{
    who_recognition->detect_enable(false);
    return 0;
}

int at_print_exposure_info(int argc, char **argv)
{
    printf("Exposure min: %d, max: %d, default: %d\n", 0x2f, 0x60, 0x50);
    return 0;
}

int at_set_exposure_info(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&at_exposure_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, at_exposure_args.end, argv[0]);
        return 1;
    }

    const char *param_value = at_exposure_args.exposure->sval[0];  // 拿到参数

    if (!is_all_digits(param_value)) {
        printf("Error: Parameter must be a numeric ID, but got '%s'\n", param_value);
        return 1;  // 返回失败
    }

    int exposure_time = atoi(param_value);

    return cam->set_exposure_time(exposure_time);
}

static uint8_t triggerDump = 1;
typedef struct {
    uint32_t ulRunTimeCounter;
    uint32_t xTaskNumber;
} taskData_t;

#define TASK_MAX_COUNT 32
static taskData_t previousSnapshot[TASK_MAX_COUNT]; 
static int taskTopIndex = 0;
static uint32_t previousTotalRunTime = 0;
static StaticTimer_t timerBuffer;

static taskData_t *getPreviousTaskData(uint32_t xTaskNumber)
{
    // Try to find the task in the list of tasks
    for (int i = 0; i < taskTopIndex; i++) {
        if (previousSnapshot[i].xTaskNumber == xTaskNumber) {
            return &previousSnapshot[i];
        }
    }

    // Allocate a new entry
    ESP_ERROR_CHECK(!(taskTopIndex < TASK_MAX_COUNT)); taskData_t *result = &previousSnapshot[taskTopIndex]; result->xTaskNumber = xTaskNumber;
    taskTopIndex++;
    return result;
}

static void task_monitor()
{
    //if want use it ,must set CONFIG_FREERTOS_USE_TRACE_FACILITY=y
    if (triggerDump != 0) {
        uint32_t totalRunTime;
        TaskStatus_t taskStats[TASK_MAX_COUNT];
        uint32_t taskCount = uxTaskGetSystemState(taskStats, TASK_MAX_COUNT, &totalRunTime);
        ESP_ERROR_CHECK(!(taskTopIndex < TASK_MAX_COUNT));
        uint32_t totalDelta = totalRunTime - previousTotalRunTime;
        float f = 100.0 / totalDelta;

        // Dumps the the CPU load and stack usage for all tasks
        // CPU usage is since last dump in % compared to total time spent in tasks.Note that time spent in interrupts will be included in measured time.
        // Stack usage is displayed as nr of unused bytes at peak stack usage.

        ESP_LOGI(TAG, "Task dump");
        ESP_LOGI(TAG, "Load\tStack left\tName\tPRI");
        for (uint32_t i = 0; i < taskCount; i++) {
            TaskStatus_t *stats = &taskStats[i];
            taskData_t *previousTaskData = getPreviousTaskData(stats->xTaskNumber);
            uint32_t taskRunTime = stats->ulRunTimeCounter;
            float load = f * (taskRunTime - previousTaskData->ulRunTimeCounter);
            ESP_LOGI(TAG, "%.2f \t%u \t%s \t%u", load, stats->usStackHighWaterMark, stats->pcTaskName, stats->uxBasePriority);
            previousTaskData->ulRunTimeCounter = taskRunTime;
        }
        ESP_LOGI(TAG, "Internal free heap: %d\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        previousTotalRunTime = totalRunTime;
    }
}


void monitor_task(void *arg)
{
    while (1)
    {
        task_monitor();
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
    
}

extern "C" void app_main(void)
{
#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));

#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_console_dev_usb_cdc_config_t hw_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&hw_config, &repl_config, &repl));

#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));

#else
#error Unsupported console type
#endif

#if CONFIG_DB_FATFS_FLASH
    ESP_ERROR_CHECK(fatfs_flash_mount());
#endif
#if CONFIG_DB_SPIFFS
    ESP_ERROR_CHECK(bsp_spiffs_mount());
#endif
#if CONFIG_DB_FATFS_SDCARD || CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD || CONFIG_HUMAN_FACE_FEAT_MODEL_IN_SDCARD
    ESP_ERROR_CHECK(bsp_sdcard_mount());
#endif
#if CONFIG_IDF_TARGET_ESP32P4
    cam = new P4Cam(VIDEO_PIX_FMT_YUV422P, 5, V4L2_MEMORY_MMAP, true);

    printf("res: %d x %d\n", cam->m_width, cam->m_height);

    app_uvc_display_main(cam->m_width, cam->m_height);

#elif CONFIG_IDF_TARGET_ESP32S3
    auto cam = new S3Cam(PIXFORMAT_RGB565, FRAMESIZE_240X240, 4, true);
#endif
    auto who_cam_lcd = new WhoCamLCD(cam);
#if CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD || CONFIG_HUMAN_FACE_FEAT_MODEL_IN_SDCARD
    char dir[64];
#if CONFIG_IDF_TARGET_ESP32P4
    snprintf(dir, sizeof(dir), "%s/espdl_models/p4", CONFIG_BSP_SD_MOUNT_POINT);
#elif CONFIG_IDF_TARGET_ESP32S3
    snprintf(dir, sizeof(dir), "%s/espdl_models/s3", CONFIG_BSP_SD_MOUNT_POINT);
#endif
#endif

#if !CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD
    HumanFaceDetect *human_face_detect = new HumanFaceDetect();
#else
    HumanFaceDetect *human_face_detect = new HumanFaceDetect(dir);
#endif

#if !CONFIG_HUMAN_FACE_FEAT_MODEL_IN_SDCARD
    HumanFaceFeat *human_face_feat = new HumanFaceFeat();
#else
    HumanFaceFeat *human_face_feat = new HumanFaceFeat(dir);
#endif

    char db_path[64];
#if CONFIG_DB_FATFS_FLASH
    snprintf(db_path, sizeof(db_path), "%s/face.db", CONFIG_SPIFLASH_MOUNT_POINT);
#elif CONFIG_DB_SPIFFS
    snprintf(db_path, sizeof(db_path), "%s/face.db", CONFIG_BSP_SPIFFS_MOUNT_POINT);
#else
    snprintf(db_path, sizeof(db_path), "%s/face.db", CONFIG_BSP_SD_MOUNT_POINT);
#endif
    auto human_face_recognizer = new HumanFaceRecognizer(
        human_face_feat, db_path, static_cast<dl::recognition::db_type_t>(CONFIG_DB_FILE_SYSTEM));

    who_recognition = new WhoHumanFaceRecognition(human_face_detect, human_face_recognizer, cam);
    who_cam_lcd->run();
    who_recognition->run();

    const esp_console_cmd_t at_enroll = {
        .command = "AT+Enroll",
        .help = "Enroll the current face. Make sure to enroll only when a face is successfully detected.",
        .hint = NULL,
        .func = at_enroll_func,
        .argtable = NULL,
    };

    const esp_console_cmd_t at_recognize_start = {
        .command = "AT+RecognizeStart",
        .help = "Start recognize face",
        .hint = NULL,
        .func = at_recognize_start_func,
        .argtable = NULL,
    };

    const esp_console_cmd_t at_recognize_stop = {
        .command = "AT+RecognizeStop",
        .help = "Stop recognize face",
        .hint = NULL,
        .func = at_recognize_stop_func,
        .argtable = NULL,
    };

    const esp_console_cmd_t at_reset = {
        .command = "AT+RESTORE",
        .help = "System full reboot",
        .hint = NULL,
        .func = at_reset_func,
        .argtable = NULL,
    };

    const esp_console_cmd_t at_get_all_registered_id = {
        .command = "AT+Enroll?",
        .help = "Get the number of currently enrolled faces.",
        .hint = NULL,
        .func = at_get_all_registered_id_func,
        .argtable = NULL,
    };

    const esp_console_cmd_t at_face_start = {
        .command = "AT+Facestart",
        .help = "Start face detection",
        .hint = NULL,
        .func = at_face_start_func,
        .argtable = NULL,
    };

    const esp_console_cmd_t at_face_stop = {
        .command = "AT+Facestop",
        .help = "Stop face detection",
        .hint = NULL,
        .func = at_face_stop_func,
        .argtable = NULL,
    };

    at_delete_args.id = arg_str1(NULL, NULL, "<id>", "Input enrolled ID");
    at_delete_args.end = arg_end(1);

    const esp_console_cmd_t at_delete = {
        .command = "AT+Delete",
        .help = "Delete face with specified ID",
        .hint = NULL,
        .func = at_delete_func,
        .argtable = &at_delete_args,
    };

    const esp_console_cmd_t at_print_exposure_info_cmd = {
        .command = "AT+Exposure?",
        .help = "Print exposure time details",
        .hint = NULL,
        .func = at_print_exposure_info,
        .argtable = NULL,
    };

    at_exposure_args.exposure = arg_str1(NULL, NULL, "<exposure time>", "Input exposure time");
    at_exposure_args.end = arg_end(1);

    const esp_console_cmd_t at_set_exposure_info_cmd = {
        .command = "AT+Exposure",
        .help = "Set exposure time. Make sure it stays within the allowed range.",
        .hint = NULL,
        .func = at_set_exposure_info,
        .argtable = &at_exposure_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&at_enroll));
    ESP_ERROR_CHECK(esp_console_cmd_register(&at_recognize_start));
    ESP_ERROR_CHECK(esp_console_cmd_register(&at_recognize_stop));
    ESP_ERROR_CHECK(esp_console_cmd_register(&at_reset));
    ESP_ERROR_CHECK(esp_console_cmd_register(&at_get_all_registered_id));
    ESP_ERROR_CHECK(esp_console_cmd_register(&at_face_start));
    ESP_ERROR_CHECK(esp_console_cmd_register(&at_face_stop));
    ESP_ERROR_CHECK(esp_console_cmd_register(&at_delete));
    ESP_ERROR_CHECK(esp_console_cmd_register(&at_print_exposure_info_cmd));
    ESP_ERROR_CHECK(esp_console_cmd_register(&at_set_exposure_info_cmd));

    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    xTaskCreatePinnedToCore(monitor_task, "monitor_task", 4096, NULL, 2, NULL, 1);

#if !CONFIG_DB_FATFS_SDCARD && (CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD || CONFIG_HUMAN_FACE_FEAT_MODEL_IN_SDCARD)
    ESP_ERROR_CHECK(bsp_sdcard_unmount());
#endif
}
