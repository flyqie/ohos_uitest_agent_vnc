#include "uitest.h"

#include <window_manager/oh_display_manager.h>

ScreenCopyCallback g_screenCopyCallback;

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
    if (g_screenCopyCallback != NULL && bytes.data != NULL && bytes.size > 0) {
        g_screenCopyCallback((char*)bytes.data, (int)bytes.size);
    }
}

int UiTest_StartScreenCopy(ScreenCopyCallback callback) {
    if (g_LowLevelFunctions.startCapture == NULL || callback == NULL) {
        AGENT_OHOS_LOG(LOG_ERROR, "UiTest_StartScreenCopy: g_LowLevelFunctions is nullptr");
        return -1;
    }

    g_screenCopyCallback = callback;
    struct Text name = { .data = "copyScreen" };
    name.size = sizeof(name.data);
    struct Text optJson = {};
    if (g_LowLevelFunctions.startCapture(name, UiTest_onScreenCopy, optJson) == RETCODE_SUCCESS) {
        return 0;
    }
    return -2;
}

int UiTest_StopScreenCopy() {
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
