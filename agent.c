#include "agent.h"

struct UiTestPort g_UiTestPort;
struct LowLevelFunctions g_LowLevelFunctions;
rfbScreenInfoPtr g_rfbServer;

void AGENT_OHOS_LOG(LogLevel level, const char* msg) {
    OH_LOG_Print(LOG_APP, level, 0, "UiTestKit_Agent", "%{public}s", msg);
}

void rfbServerLogInfoToString(const char *format, ...) {
    int bufferSize = 4096;
    char buffer[bufferSize];
    va_list argPtr;
    va_start(argPtr, format);
    vsnprintf(buffer, bufferSize, format, argPtr);
    va_end(argPtr);
    AGENT_OHOS_LOG(LOG_INFO, buffer);
}

void rfbServerLogErrToString(const char *format, ...) {
    int bufferSize = 4096;
    char buffer[bufferSize];
    va_list argPtr;
    va_start(argPtr, format);
    vsnprintf(buffer, bufferSize, format, argPtr);
    va_end(argPtr);
    AGENT_OHOS_LOG(LOG_ERROR, buffer);
}

void setServerRfbLog() {
    rfbLog = rfbServerLogInfoToString;
    rfbErr = rfbServerLogErrToString;
}

// 入口函数
RetCode UiTestExtension_OnInit(struct UiTestPort port, size_t argc, char **argv) {
    AGENT_OHOS_LOG(LOG_INFO, "UiTestExtension_OnInit START");
    g_UiTestPort = port;
    port.initLowLevelFunctions(&g_LowLevelFunctions);

    setServerRfbLog();
    g_rfbServer = rfbGetScreen((int*)&argc, argv, 400, 300, 8, 3, 4);
    if (!g_rfbServer) {
        AGENT_OHOS_LOG(LOG_FATAL, "rfbGetScreen failed");
        return RETCODE_FAIL;
    }
    g_rfbServer->frameBuffer = (char *) malloc(400 * 300 * 4);
    if (!g_rfbServer->frameBuffer) {
        AGENT_OHOS_LOG(LOG_FATAL, "malloc frameBuffer failed");
        rfbScreenCleanup(g_rfbServer);
        return RETCODE_FAIL;
    }
    AGENT_OHOS_LOG(LOG_INFO, "UiTestExtension_OnInit END");
    return RETCODE_SUCCESS;
}

// 执行函数
RetCode UiTestExtension_OnRun() {
    AGENT_OHOS_LOG(LOG_INFO, "UiTestExtension_OnRun START");
    rfbInitServer(g_rfbServer);
    rfbRunEventLoop(g_rfbServer,-1,FALSE);
    AGENT_OHOS_LOG(LOG_INFO, "UiTestExtension_OnRun END");
    return RETCODE_SUCCESS;
}
