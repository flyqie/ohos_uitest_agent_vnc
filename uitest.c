#include "uitest.h"

#include <errno.h>
#include <window_manager/oh_display_manager.h>
#include <window_manager/oh_display_capture.h>
#include <multimedia/image_framework/image/pixelmap_native.h>
#include <fcntl.h>
#include <sys/stat.h>

ScreenCopyCallback g_screenCopyCallback;

bool g_screenCopyPNGThreadRun = 0;
bool g_screenCopyDMPUBThreadRun = 0;
char g_screenCopyMode[16] = {};
int g_fps;

int UiTest_getScreenWidth() {
    int32_t width;
    if (OH_NativeDisplayManager_GetDefaultDisplayWidth(&width) != DISPLAY_MANAGER_OK) {
        return RETCODE_FAIL;
    }
    return width;
}

int UiTest_getScreenHeight() {
    int32_t height;
    if (OH_NativeDisplayManager_GetDefaultDisplayHeight(&height) != DISPLAY_MANAGER_OK) {
        return RETCODE_FAIL;
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
    AGENT_OHOS_LOG(LOG_DEBUG, "%s: frame time %.3f ms, sleep %.3f ms", __func__, (double)elapsed_us / 1000.0, (double)sleep_us / 1000.0);
    if (elapsed_us < frame_interval_us) {
        AGENT_OHOS_LOG(LOG_DEBUG, "%s: elapsed_us < frame_interval_us, skip frame", __func__);
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
    AGENT_OHOS_LOG(LOG_INFO, "%s: %s", __func__, output.data);
}

void UiTest_ScreenCopyPNGTask() {
    // 20MB大缓冲区
    char *png_buffer = malloc(1024 * 1024 * 20);
    if (!png_buffer) {
        AGENT_OHOS_LOG(LOG_ERROR, "%s: png_buffer malloc failed", __func__);
        g_screenCopyPNGThreadRun = false;
        return;
    }

    UiTest_CreateDriver();
    AGENT_OHOS_LOG(LOG_INFO, "%s: Start", __func__);

    const long frame_interval_us = 1000000 / g_fps;

    while (g_screenCopyPNGThreadRun) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        int fd = open("/data/local/tmp/uitest_agent_vnc_cap.png", O_RDWR | O_CREAT, 0666);
        if (fd < 0) {
            AGENT_OHOS_LOG(LOG_ERROR, "%s: open file failed", __func__);
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
            AGENT_OHOS_LOG(LOG_ERROR, "%s: reopen failed (%s)(%s)", __func__, strerror(errno), output.data);
            break;
        }
        struct stat st;
        if (fstat(fd, &st) != 0) {
            AGENT_OHOS_LOG(LOG_ERROR, "%s: fstat failed (%s)(%s)", __func__, strerror(errno), output.data);
            close(fd);
            break;
        }
        if (st.st_size <= 2) {
            AGENT_OHOS_LOG(LOG_ERROR, "%s: st_size <= 2 (%s)", __func__, output.data);
            close(fd);
            break;
        }
        lseek(fd, 0, SEEK_SET);
        ssize_t n = read(fd, png_buffer, st.st_size);
        close(fd);
        if (n != st.st_size) {
            AGENT_OHOS_LOG(LOG_ERROR, "%s: read n != st.st_size", __func__);
            break;
        }
        AGENT_OHOS_LOG(LOG_DEBUG, "%s: Read screenshot: %zd bytes", __func__, n);
        if (g_screenCopyCallback != NULL) {
            g_screenCopyCallback(png_buffer, (int)n);
        }

        clock_gettime(CLOCK_MONOTONIC, &end);
        long elapsed_us = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_nsec - start.tv_nsec) / 1000;
        long sleep_us = frame_interval_us - elapsed_us;
        AGENT_OHOS_LOG(LOG_DEBUG, "%s: frame time %.3f ms, sleep %.3f ms", __func__, (double)elapsed_us / 1000.0, (double)sleep_us / 1000.0);
        if (sleep_us > 0) {
            usleep(sleep_us);
        }
    }

    AGENT_OHOS_LOG(LOG_INFO, "%s: Stop", __func__);
    free(png_buffer);
    g_screenCopyPNGThreadRun = true;
}

void UiTest_ScreenCopyDMPUBTask() {
    // 复用缓冲区
    size_t rgb_buffer_size = UiTest_getScreenHeight() * UiTest_getScreenWidth() * 4;
    char *rgb_buffer = malloc(rgb_buffer_size);
    if (!rgb_buffer) {
        AGENT_OHOS_LOG(LOG_ERROR, "%s: rgb_buffer malloc failed", __func__);
        g_screenCopyDMPUBThreadRun = false;
        return;
    }
    AGENT_OHOS_LOG(LOG_INFO, "%s: Start", __func__);
    const long frame_interval_us = 1000000 / g_fps;

    while (g_screenCopyDMPUBThreadRun) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        NativeDisplayManager_ErrorCode dmRet;

        uint64_t displayId = 0;
        dmRet = OH_NativeDisplayManager_GetDefaultDisplayId(&displayId);
        if (dmRet != DISPLAY_MANAGER_OK) {
            AGENT_OHOS_LOG(LOG_ERROR, "%s: GetDefaultDisplayId failed %d", __func__, dmRet);
            break;
        }
        OH_PixelmapNative *pixelMap = NULL;
        uint32_t displayId32 = (uint32_t)displayId;
        dmRet = OH_NativeDisplayManager_CaptureScreenPixelmap(displayId32, &pixelMap);
        if (dmRet != DISPLAY_MANAGER_OK) {
            AGENT_OHOS_LOG(LOG_ERROR, "%s: CaptureScreenPixelmap failed %d", __func__, dmRet);
            break;
        }
        Image_ErrorCode pmRet = OH_PixelmapNative_ReadPixels(pixelMap, (uint8_t*)rgb_buffer, &rgb_buffer_size);
        if (pmRet != IMAGE_SUCCESS) {
            AGENT_OHOS_LOG(LOG_ERROR, "%s: ReadPixels failed %d", __func__, dmRet);
            break;
        }
        AGENT_OHOS_LOG(LOG_DEBUG, "%s: Read screenshot: %zd bytes", __func__, rgb_buffer_size);
        if (g_screenCopyCallback != NULL) {
            g_screenCopyCallback(rgb_buffer, (int)rgb_buffer_size);
        }
        OH_PixelmapNative_Destroy(&pixelMap);

        clock_gettime(CLOCK_MONOTONIC, &end);
        long elapsed_us = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_nsec - start.tv_nsec) / 1000;
        long sleep_us = frame_interval_us - elapsed_us;
        AGENT_OHOS_LOG(LOG_DEBUG, "%s: frame time %.3f ms, sleep %.3f ms", __func__, (double)elapsed_us / 1000.0, (double)sleep_us / 1000.0);
        if (sleep_us > 0) {
            usleep(sleep_us);
        }
    }

    AGENT_OHOS_LOG(LOG_INFO, "%s: Stop", __func__);
    free(rgb_buffer);
    g_screenCopyDMPUBThreadRun = false;
}

int UiTest_StartScreenCopy(ScreenCopyCallback callback, char mode[16], int fps) {
    if (callback == NULL) {
        AGENT_OHOS_LOG(LOG_ERROR, "%s: callback is nullptr", __func__);
        return -1;
    }
    g_screenCopyCallback = callback;
    g_fps = fps;
    snprintf(g_screenCopyMode, sizeof(g_screenCopyMode), "%s", mode);

    if (strcmp(mode, CAP_MODE_PNG) == 0) {
        AGENT_OHOS_LOG(LOG_INFO, "%s: Start PNG Screen Copy Task", __func__);
        if (g_screenCopyPNGThreadRun) {
            AGENT_OHOS_LOG(LOG_ERROR, "%s: PNG Thread already running", __func__);
            return RETCODE_FAIL;
        }
        g_screenCopyPNGThreadRun = true;
        pthread_t thread;
        if (pthread_create(&thread, NULL, (void* (*)(void*))UiTest_ScreenCopyPNGTask, NULL) != 0) {
            AGENT_OHOS_LOG(LOG_ERROR, "%s: Create PNG Thread failed", __func__);
            g_screenCopyPNGThreadRun = false;
            return RETCODE_FAIL;
        }
        pthread_detach(thread);
    }else if (strcmp(mode, CAP_MODE_DMPUB) == 0) {
        // Display Manager Public Api
        if (g_screenCopyDMPUBThreadRun) {
            AGENT_OHOS_LOG(LOG_ERROR, "%s: DMPUB Thread already running", __func__);
            return RETCODE_FAIL;
        }
        g_screenCopyDMPUBThreadRun = true;
        pthread_t thread;
        if (pthread_create(&thread, NULL, (void* (*)(void*))UiTest_ScreenCopyDMPUBTask, NULL) != 0) {
            AGENT_OHOS_LOG(LOG_ERROR, "%s: Create DMPUB Thread failed", __func__);
            g_screenCopyDMPUBThreadRun = false;
            return RETCODE_FAIL;
        }
        pthread_detach(thread);
    } else {
        AGENT_OHOS_LOG(LOG_INFO, "%s: Start JPEG Screen Copy Task", __func__);
        if (g_LowLevelFunctions.startCapture == NULL) {
            AGENT_OHOS_LOG(LOG_ERROR, "%s: g_LowLevelFunctions is nullptr", __func__);
            return RETCODE_FAIL;
        }

        struct Text name = { .data = "copyScreen" };
        name.size = sizeof(name.data);
        struct Text optJson = {};
        if (g_LowLevelFunctions.startCapture(name, UiTest_onScreenCopy, optJson) != RETCODE_SUCCESS) {
            AGENT_OHOS_LOG(LOG_ERROR, "%s: g_LowLevelFunctions.startCapture failed", __func__);
            return RETCODE_FAIL;
        }
    }
    return RETCODE_SUCCESS;
}

int UiTest_StopScreenCopy() {
    if (strcmp(g_screenCopyMode, CAP_MODE_PNG) == 0) {
        AGENT_OHOS_LOG(LOG_INFO, "%s: Stop PNG Screen Copy Task", __func__);
        g_screenCopyPNGThreadRun = false;
        sleep(2);
    }else if (strcmp(g_screenCopyMode, CAP_MODE_DMPUB) == 0) {
        g_screenCopyDMPUBThreadRun = false;
        sleep(2);
    } else {
        AGENT_OHOS_LOG(LOG_INFO, "%s: Stop JPEG Screen Copy Task", __func__);
        if (g_LowLevelFunctions.startCapture == NULL) {
            AGENT_OHOS_LOG(LOG_ERROR, "%s: g_LowLevelFunctions is nullptr", __func__);
            return RETCODE_FAIL;
        }

        g_screenCopyCallback = NULL;
        struct Text name = { .data = "copyScreen" };
        name.size = sizeof(name.data);
        if (g_LowLevelFunctions.stopCapture(name) != RETCODE_SUCCESS) {
            AGENT_OHOS_LOG(LOG_ERROR, "%s: g_LowLevelFunctions.stopCapture failed", __func__);
            return RETCODE_FAIL;
        }
    }
    return RETCODE_SUCCESS;
}

int UiTest_InjectionPtr(enum ActionStage stage, int x, int y) {
    if (g_LowLevelFunctions.atomicTouch == NULL) {
        AGENT_OHOS_LOG(LOG_ERROR, "%s: g_LowLevelFunctions is nullptr", __func__);
        return RETCODE_FAIL;
    }
    if (g_LowLevelFunctions.atomicTouch(stage, x, y) != RETCODE_SUCCESS) {
        AGENT_OHOS_LOG(LOG_ERROR, "%s: g_LowLevelFunctions.atomicTouch failed", __func__);
        return RETCODE_FAIL;
    }
    return RETCODE_SUCCESS;
}
