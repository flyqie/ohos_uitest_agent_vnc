#include "agent.h"
#include "uitest.h"
#include <jpeglib.h>

struct UiTestPort g_UiTestPort;
struct LowLevelFunctions g_LowLevelFunctions;
BufferManager* g_BufferManager;
int screenW, screenH;

void AGENT_OHOS_LOG(LogLevel level, const char* fmt, ...) {
    char buffer[4096];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    OH_LOG_Print(LOG_APP, level, 0, "UiTestKit_Agent", "%{public}s", buffer);
}

void rfbServerLogInfoToString(const char *format, ...) {
    char buffer[4096];
    va_list argPtr;
    va_start(argPtr, format);
    vsnprintf(buffer, sizeof(buffer), format, argPtr);
    va_end(argPtr);
    AGENT_OHOS_LOG(LOG_INFO, buffer);
}

void rfbServerLogErrToString(const char *format, ...) {
    char buffer[4096];
    va_list argPtr;
    va_start(argPtr, format);
    vsnprintf(buffer, sizeof(buffer), format, argPtr);
    va_end(argPtr);
    AGENT_OHOS_LOG(LOG_ERROR, buffer);
}

void setServerRfbLog() {
    rfbLog = rfbServerLogInfoToString;
    rfbErr = rfbServerLogErrToString;
}

/**
 * 申请修改双缓冲区
 * 注意: 该函数会锁定双缓冲区, 所以请务必调用release_vnc_buf进行切换
 *
 * @param manager
 * @return 双缓冲区内存地址
 */
char *request_back_vnc_buf(BufferManager *manager) {
    pthread_mutex_lock(&manager->backBufferLock);
    pthread_mutex_lock(&manager->backBufferFuncLock);
    char *buffer = manager->backBuffer;
    pthread_mutex_unlock(&manager->backBufferFuncLock);
    return buffer;
}

/**
 * 释放双缓冲区
 * 注意: 该函数会解锁双缓冲区, 所以请务必先调用request_back_vnc_buf来获取双缓冲区
 *
 * @param manager
 * @param w1 需要发送到客户端修改的起始宽度
 * @param y1 需要发送到客户端修改的起始高度
 * @param w2 需要发送到客户端修改的终止宽度
 * @param y2 需要发送到客户端修改的终止宽度
 * @return
 */
int release_vnc_buf(BufferManager *manager, int w1, int y1, int w2, int y2) {
    pthread_rwlock_wrlock(&manager->frontBufferLock);
    pthread_mutex_lock(&manager->backBufferFuncLock);
    char *temp = manager->frontBuffer;
    manager->frontBuffer = manager->backBuffer;
    manager->backBuffer = temp;
    manager->server->frameBuffer = manager->frontBuffer;
    pthread_mutex_unlock(&manager->backBufferLock);
    pthread_rwlock_unlock(&manager->frontBufferLock);
    rfbMarkRectAsModified(manager->server, w1, y1, w2, y2);
    pthread_mutex_unlock(&manager->backBufferFuncLock);
    return 0;
}

void key_event(rfbBool down, rfbKeySym key, rfbClientPtr cl) {
    //
}

void ptr_event(int buttonMask, int x, int y, rfbClientPtr cl) {
    // PTR!
}

/**
 * 初始化vnc服务器, 该函数会同步创建双缓冲区
 * 注意: 请务必在不需要时调用cleanup_vnc_server释放内存, 否则会造成内存泄露!
 *
 * @param width 屏幕宽度
 * @param height 屏幕高度
 * @param bits_per_pixel BPP
 * @param port vnc端口
 * @param desktopName 桌面名称
 * @param password vnc密码, 为空为无鉴权
 * @param argc
 * @param argv
 * @return
 */
BufferManager *
init_vnc_server(const int width, const int height, const int bits_per_pixel, const int port, const char *desktopName,
                const char *password, int* argc, char** argv) {
    BufferManager *manager = calloc(1, sizeof(BufferManager));
    manager->bufferSize = width * height * (bits_per_pixel / 8);
    manager->frontBuffer = (char *) calloc(1, manager->bufferSize);
    manager->backBuffer = (char *) calloc(1, manager->bufferSize);
    manager->server = rfbGetScreen(argc, argv, width, height, 8, 4, (bits_per_pixel / 8));
    manager->server->frameBuffer = manager->frontBuffer;
    manager->server->desktopName = strdup(desktopName);
    manager->server->alwaysShared = TRUE;
    manager->server->httpDir = NULL;
    manager->server->port = port;
    // manager->server->kbdAddEvent = key_event;
    manager->server->ptrAddEvent = ptr_event;
    // 密码设置
    if (password != NULL && *password != '\0') {
        char **passwordList = malloc(sizeof(char **) * 2);
        passwordList[0] = strdup(password);
        passwordList[1] = NULL;
        manager->server->authPasswdData = (void *) passwordList;
        manager->server->passwordCheck = rfbCheckPasswordByList;
    }

    rfbInitServer(manager->server);
    /* Mark as dirty since we haven't sent any updates at all yet. */
    rfbMarkRectAsModified(manager->server, 0, 0, width, height);
    pthread_rwlock_init(&manager->frontBufferLock, NULL);
    pthread_mutex_init(&manager->backBufferLock, NULL);
    pthread_mutex_init(&manager->backBufferFuncLock, NULL);

    return manager;
}

/**
 * 停止vnc服务器
 * 建议停止后等待几秒再进行清理
 *
 * @param manager
 * @return
 */
int stop_vnc_server(BufferManager *manager) {
    manager->stop_vnc_server_flag = 1;
    return 0;
}

/**
 * 运行vnc服务器
 * 注意: 该函数为阻塞函数
 *
 * @param manager
 * @return
 */
int run_vnc_server(BufferManager *manager) {
    int hasRLock;
    manager->stop_vnc_server_flag = 0;
    manager->stopped_vnc_server_flag = 0;
    while (!manager->stop_vnc_server_flag) {
        if (manager->server->clientHead != NULL) {
            if(manager->have_client_flag != 1) {
                manager->have_client_flag = 1;
            }
            hasRLock = 1;
        } else {
            if(manager->have_client_flag != 0) {
                manager->have_client_flag = 0;
            }
            hasRLock = 0;
        }
        if (hasRLock) {
            pthread_rwlock_rdlock(&manager->frontBufferLock);
        }
        rfbProcessEvents(manager->server, -1);
        if (hasRLock) {
            pthread_rwlock_unlock(&manager->frontBufferLock);
        }
    }
    rfbScreenCleanup(manager->server);
    manager->stopped_vnc_server_flag = 1;
    return 0;
}

/**
 * 清理vnc服务器
 * 注意: 请务必在vnc服务器停止后调用
 *
 * @param manager
 * @return
 */
int cleanup_vnc_server(BufferManager *manager) {
    if (manager->stop_vnc_server_flag != 1 || manager->stopped_vnc_server_flag != 1) {
        return -1;
    }
    pthread_rwlock_destroy(&manager->frontBufferLock);
    pthread_mutex_destroy(&manager->backBufferLock);
    pthread_mutex_destroy(&manager->backBufferFuncLock);
    free(manager->frontBuffer);
    free(manager->backBuffer);
    free(manager->server->screenData);
    free(manager);
    return 0;
}

// 极致优化：只用last_frame，逐行解码比对，变化行直接整行memcpy，锁严格配对
static unsigned char* last_frame = NULL;
static int last_frame_w = 0, last_frame_h = 0;

void screenJpegCallback(char* data, int size) {
    if (!g_BufferManager) return;
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char*)data, size);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);
    int screenW_local = g_BufferManager->server->width;
    int screenH_local = g_BufferManager->server->height;
    int jpegW = (int)cinfo.output_width;
    int jpegH = (int)cinfo.output_height;
    int row_stride = jpegW * cinfo.output_components; // 通常为3
    int fb_stride = screenW_local * 4;
    char* fb = request_back_vnc_buf(g_BufferManager);
    unsigned char* buffer = (unsigned char*)malloc(row_stride);
    int y;
    // 先填充JPEG实际内容
    for (y = 0; y < jpegH && y < screenH_local; ++y) {
        unsigned char* rowptr = buffer;
        jpeg_read_scanlines(&cinfo, &rowptr, 1);
        for (int x = 0; x < screenW_local; ++x) {
            unsigned char r = 0, g = 0, b = 0;
            if (x < jpegW) {
                r = buffer[x * 3 + 0];
                g = buffer[x * 3 + 1];
                b = buffer[x * 3 + 2];
            }
            char* fb_row = &fb[y * fb_stride + x * 4];
            fb_row[0] = r;
            fb_row[1] = g;
            fb_row[2] = b;
            fb_row[3] = 0xFF;
        }
    }
    // 剩余framebuffer行全部补0
    for (; y < screenH_local; ++y) {
        for (int x = 0; x < screenW_local; ++x) {
            char* fb_row = &fb[y * fb_stride + x * 4];
            fb_row[0] = 0;
            fb_row[1] = 0;
            fb_row[2] = 0;
            fb_row[3] = 0xFF;
        }
    }
    free(buffer);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    release_vnc_buf(g_BufferManager, 0, 0, screenW_local, screenH_local);
}

// 入口函数
RetCode UiTestExtension_OnInit(struct UiTestPort port, size_t argc, char **argv) {
    AGENT_OHOS_LOG(LOG_INFO, "UiTestExtension_OnInit START");
    g_UiTestPort = port;
    port.initLowLevelFunctions(&g_LowLevelFunctions);
    screenW = UiTest_getScreenWidth();
    screenH = UiTest_getScreenHeight();
    if (screenH <= 0 || screenW <= 0) {
        AGENT_OHOS_LOG(LOG_FATAL, "Get Screen Size Failed");
        return RETCODE_FAIL;
    }
    AGENT_OHOS_LOG(LOG_FATAL, "Screen Size: %dx%d", screenW, screenH);
    setServerRfbLog();
    int _argc = (int)argc;
    g_BufferManager = init_vnc_server(screenW, screenH, 32, 5900, "HiVnc", NULL, &_argc, argv);
    AGENT_OHOS_LOG(LOG_INFO, "UiTestExtension_OnInit END");
    return RETCODE_SUCCESS;
}

// 执行函数
RetCode UiTestExtension_OnRun() {
    AGENT_OHOS_LOG(LOG_INFO, "UiTestExtension_OnRun START");
    if (g_BufferManager == NULL) {
        AGENT_OHOS_LOG(LOG_FATAL, "g_BufferManager NULL??");
        return RETCODE_FAIL;
    }
    UiTest_StartScreenCopy(screenJpegCallback);
    run_vnc_server(g_BufferManager);
    cleanup_vnc_server(g_BufferManager);
    AGENT_OHOS_LOG(LOG_INFO, "UiTestExtension_OnRun END");
    return RETCODE_SUCCESS;
}
