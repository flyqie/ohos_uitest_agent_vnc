#include "uitest.h"

#include <errno.h>
#include <window_manager/oh_display_manager.h>
#include <fcntl.h>
#include <sys/stat.h>

ScreenCopyCallback g_screenCopyCallback;

int g_screenCopyPNGThreadRun = 0;
int g_screenCopyMode = 0;
int g_fps;

int UiTest_getScreenWidth() {
    int32_t width;
    if (OH_NativeDisplayManager_GetDefaultDisplayWidth(&width) != DISPLAY_MANAGER_OK) {
        return -2;
    }
    return width;
}

int UiTest_getScreenHeight() {
    int32_t height;
    if (OH_NativeDisplayManager_GetDefaultDisplayHeight(&height) != DISPLAY_MANAGER_OK) {
        return -2;
    }
    return height;
}

void UiTest_onScreenCopy(struct Text bytes) {
    // -1 表示未初始化
    static int64_t last_us = -1;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t now_us = (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
    if (last_us < 0) {
        last_us = now_us;
        return;
    }
    int64_t elapsed_us = now_us - last_us;
    int64_t frame_interval_us = 1000000 / g_fps;
    int64_t sleep_us = frame_interval_us - elapsed_us;
    AGENT_OHOS_LOG(LOG_DEBUG, "UiTest_onScreenCopy: frame time %.3f ms, sleep %.3f ms", (double)elapsed_us / 1000.0, (double)sleep_us / 1000.0);
    if (elapsed_us < frame_interval_us) {
        AGENT_OHOS_LOG(LOG_DEBUG, LOG_TAG, "elapsed_us < frame_interval_us, skip frame");
        return;
    }
    last_us = now_us;

    if (g_screenCopyCallback != NULL && bytes.data != NULL && bytes.size > 0) {
        g_screenCopyCallback((char*)bytes.data, (int)bytes.size);
    }
}

static void UiTest_CreateDriver() {
    struct Text input = {.data = "{\"api\":\"Driver.create\",\"this\":null,\"args\":[]}"};
    input.size = sizeof(input.data);
    uint8_t outputData[2048] = {};
    size_t outputSize;
    struct ReceiveBuffer output = { outputData, sizeof(outputData), &outputSize };
    int32_t fatalError = 0;
    g_LowLevelFunctions.callThroughMessage(input, output, &fatalError);
    AGENT_OHOS_LOG(LOG_INFO, "UiTest_CreateDriver: %s", output.data);
}

void UiTest_ScreenCopyPNGTask() {
    // 20MB大缓冲区
    char *png_buffer = malloc(1024 * 1024 * 20);
    if (!png_buffer) {
        AGENT_OHOS_LOG(LOG_ERROR, "ScreenCopyPNGTask: png_buffer malloc failed");
        g_screenCopyPNGThreadRun = 0;
        return;
    }

    UiTest_CreateDriver();
    AGENT_OHOS_LOG(LOG_INFO, "ScreenCopyPNGTask: Start");

    const long frame_interval_us = 1000000 / g_fps;

    while (g_screenCopyPNGThreadRun) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        int fd = open("/data/local/tmp/uitest_agent_vnc_cap.png", O_RDWR | O_CREAT, 0666);
        if (fd < 0) {
            AGENT_OHOS_LOG(LOG_ERROR, "ScreenCopyPNGTask: open file failed");
            break;
        }
        char buffer[512] = {};
        snprintf(buffer, sizeof(buffer),
                 "{\"api\":\"Driver.screenCapture\",\"this\":\"Driver#0\",\"args\":[%d,{\"left\":%d,\"right\":%d,\"top\":%d,\"bottom\":%d}]}",
                 fd, 0, UiTest_getScreenWidth(), 0, UiTest_getScreenHeight());

        struct Text input = {.data = buffer, .size = sizeof(buffer)};
        uint8_t outputData[2048] = {};
        size_t outputSize;
        struct ReceiveBuffer output = { outputData, sizeof(outputData), &outputSize };
        int32_t fatalError = 0;
        g_LowLevelFunctions.callThroughMessage(input, output, &fatalError);
        // Driver.screenCapture 写入后会关闭 fd
        close(fd);
        fd = open("/data/local/tmp/uitest_agent_vnc_cap.png", O_RDONLY);
        if (fd < 0) {
            AGENT_OHOS_LOG(LOG_ERROR, "ScreenCopyPNGTask: reopen failed (%s)(%s)", strerror(errno), output.data);
            break;
        }
        struct stat st;
        if (fstat(fd, &st) != 0) {
            AGENT_OHOS_LOG(LOG_ERROR, "ScreenCopyPNGTask: fstat failed (%s)(%s)", strerror(errno), output.data);
            close(fd);
            break;
        }
        if (st.st_size <= 2) {
            AGENT_OHOS_LOG(LOG_ERROR, "ScreenCopyPNGTask: st_size <= 2 (%s)", output.data);
            close(fd);
            break;
        }
        lseek(fd, 0, SEEK_SET);
        ssize_t n = read(fd, png_buffer, st.st_size);
        close(fd);
        if (n != st.st_size) {
            AGENT_OHOS_LOG(LOG_ERROR, "ScreenCopyPNGTask: read n != st.st_size");
            break;
        }
        AGENT_OHOS_LOG(LOG_DEBUG, "Read screenshot: %zd bytes", n);
        if (g_screenCopyCallback != NULL) {
            g_screenCopyCallback(png_buffer, (int)n);
        }

        clock_gettime(CLOCK_MONOTONIC, &end);
        long elapsed_us = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_nsec - start.tv_nsec) / 1000;
        long sleep_us = frame_interval_us - elapsed_us;
        AGENT_OHOS_LOG(LOG_DEBUG, "ScreenCopyPNGTask: frame time %.3f ms, sleep %.3f ms", (double)elapsed_us / 1000.0, (double)sleep_us / 1000.0);
        if (sleep_us > 0) {
            usleep(sleep_us);
        }
    }

    AGENT_OHOS_LOG(LOG_INFO, "ScreenCopyPNGTask: Stop");
    free(png_buffer);
    g_screenCopyPNGThreadRun = 0;
}

int UiTest_StartScreenCopy(ScreenCopyCallback callback, int mode, int fps) {
    if (callback == NULL) {
        AGENT_OHOS_LOG(LOG_ERROR, "UiTest_StartScreenCopy: callback is nullptr");
        return -1;
    }
    g_screenCopyCallback = callback;
    g_screenCopyMode = mode;
    g_fps = fps;

    if (mode == 1) {
        // PNG模式
        if (g_screenCopyPNGThreadRun == 1) {
            AGENT_OHOS_LOG(LOG_ERROR, "UiTest_StartScreenCopy: PNG Thread already running");
            return -2;
        }
        g_screenCopyPNGThreadRun = 1;
        pthread_t thread;
        if (pthread_create(&thread, NULL, (void* (*)(void*))UiTest_ScreenCopyPNGTask, NULL) != 0) {
            AGENT_OHOS_LOG(LOG_ERROR, "UiTest_StartScreenCopy: Create PNG Thread failed");
            g_screenCopyPNGThreadRun = 0;
            return -3;
        }
        pthread_detach(thread);
        return 0;
    }
    if (g_LowLevelFunctions.startCapture == NULL) {
        AGENT_OHOS_LOG(LOG_ERROR, "UiTest_StartScreenCopy: g_LowLevelFunctions is nullptr");
        return -1;
    }

    struct Text name = { .data = "copyScreen" };
    name.size = sizeof(name.data);
    struct Text optJson = {};
    if (g_LowLevelFunctions.startCapture(name, UiTest_onScreenCopy, optJson) == RETCODE_SUCCESS) {
        return 0;
    }
    return -2;
}

int UiTest_StopScreenCopy() {
    if (g_screenCopyMode == 1) {
        g_screenCopyPNGThreadRun = 0;
        sleep(2);
        return 0;
    }

    if (g_LowLevelFunctions.startCapture == NULL) {
        AGENT_OHOS_LOG(LOG_ERROR, "UiTest_StopScreenCopy: g_LowLevelFunctions is nullptr");
        return -1;
    }

    g_screenCopyCallback = NULL;
    struct Text name = { .data = "copyScreen" };
    name.size = sizeof(name.data);
    if (g_LowLevelFunctions.stopCapture(name) == RETCODE_SUCCESS) {
        return 0;
    }
    return -2;
}

int UiTest_InjectionPtr(enum ActionStage stage, int x, int y) {
    if (g_LowLevelFunctions.atomicTouch == NULL) {
        AGENT_OHOS_LOG(LOG_ERROR, "UiTest_InjectionPtr: g_LowLevelFunctions is nullptr");
        return -1;
    }
    if (g_LowLevelFunctions.atomicTouch(stage, x, y) == RETCODE_SUCCESS) {
        return 0;
    }
    return -2;
}
