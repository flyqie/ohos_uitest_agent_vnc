#include "agent.h"
#include "uitest.h"
#include <deviceinfo.h>
#include <rfb/keysym.h>
#include <jpeglib.h>

struct UiTestPort g_UiTestPort;
struct LowLevelFunctions g_LowLevelFunctions;
BufferManager* g_BufferManager;

AgentConfig g_AgentConfig = {};

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

static void setServerRfbLog() {
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
static char *request_back_vnc_buf(BufferManager *manager) {
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
static int release_vnc_buf(BufferManager *manager, int w1, int y1, int w2, int y2) {
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

/**
 * 停止vnc服务器
 * 建议停止后等待几秒再进行清理
 *
 * @param manager
 * @return
 */
static int stop_vnc_server(BufferManager *manager) {
    manager->stop_vnc_server_flag = 1;
    return 0;
}

static rfbBool ctrl_down = FALSE;
void key_event(rfbBool down, rfbKeySym key, rfbClientPtr cl) {
    AGENT_OHOS_LOG(LOG_INFO, "key_event: down=%d, key=0x%08x", down, key);

    if (key == XK_Control_L || key == XK_Control_R) {
        ctrl_down = down;
        return;
    }
    if (down && ctrl_down && (key == XK_q || key == XK_Q)) {
        AGENT_OHOS_LOG(LOG_INFO, "key_event: Ctrl+Q detected! Stop Agent...");
        stop_vnc_server(g_BufferManager);
        return;
    }
}

void ptr_event(int buttonMask, int x, int y, rfbClientPtr cl) {
    //AGENT_OHOS_LOG(LOG_INFO, "ptr_event: buttonMask=0x%02x, x=%d, y=%d", buttonMask, x, y);
    static int prevMask = 0;
    static int prevX = -1, prevY = -1;

    if ((buttonMask & 1) && !(prevMask & 1)) {
        UiTest_InjectionPtr(ActionStage_DOWN, x, y);
    }
    if (!(buttonMask & 1) && (prevMask & 1)) {
        UiTest_InjectionPtr(ActionStage_UP, x, y);
    }

    if ((buttonMask & 8) && !(prevMask & 8)) {
        UiTest_InjectionPtr(ActionStage_AXIS_UP, x, y);
    }
    if ((buttonMask & 16) && !(prevMask & 16)) {
        UiTest_InjectionPtr(ActionStage_AXIS_DOWN, x, y);
    }
    if (!(buttonMask & (8|16)) && (prevMask & (8|16))) {
        UiTest_InjectionPtr(ActionStage_AXIS_STOP, x, y);
    }

    if (x != prevX || y != prevY) {
        UiTest_InjectionPtr(ActionStage_MOVE, x, y);
    }

    prevMask = buttonMask;
    prevX = x;
    prevY = y;
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
static BufferManager *
init_vnc_server(const int width, const int height, const int bits_per_pixel, const char *desktopName, int* argc, char** argv) {
    BufferManager *manager = calloc(1, sizeof(BufferManager));
    manager->bufferSize = width * height * (bits_per_pixel / 8);
    manager->frontBuffer = (char *) calloc(1, manager->bufferSize);
    manager->backBuffer = (char *) calloc(1, manager->bufferSize);
    manager->server = rfbGetScreen(argc, argv, width, height, 8, 4, (bits_per_pixel / 8));
    manager->server->frameBuffer = manager->frontBuffer;
    manager->server->desktopName = strdup(desktopName);
    manager->server->alwaysShared = TRUE;
    manager->server->httpDir = NULL;
    manager->server->kbdAddEvent = key_event;
    manager->server->ptrAddEvent = ptr_event;

    rfbInitServer(manager->server);
    /* Mark as dirty since we haven't sent any updates at all yet. */
    rfbMarkRectAsModified(manager->server, 0, 0, width, height);
    pthread_rwlock_init(&manager->frontBufferLock, NULL);
    pthread_mutex_init(&manager->backBufferLock, NULL);
    pthread_mutex_init(&manager->backBufferFuncLock, NULL);

    return manager;
}

/**
 * 运行vnc服务器
 * 注意: 该函数为阻塞函数
 *
 * @param manager
 * @return
 */
static int run_vnc_server(BufferManager *manager) {
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
static int cleanup_vnc_server(BufferManager *manager) {
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

// HUMAN NOTE: OHOS相关接口只提供了 JPEG 格式的屏幕数据, 性能较差, 没办法优化...
// AI CODE
void screenJpegCallback(char* data, int size) {
    if (!g_BufferManager) return;

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char*)data, size);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    int screenW = g_BufferManager->server->width;
    int screenH = g_BufferManager->server->height;
    int jpegW = cinfo.output_width;
    int jpegH = cinfo.output_height;
    int row_stride = jpegW * cinfo.output_components;
    int fb_stride = screenW * 4;

    // 分配 JPEG 解码缓冲
    unsigned char* buffer = (unsigned char*)malloc(row_stride);
    unsigned char* curr_frame = (unsigned char*)malloc(jpegW * jpegH * cinfo.output_components);

    for (int y = 0; y < jpegH; ++y) {
        unsigned char* rowptr = buffer;
        jpeg_read_scanlines(&cinfo, &rowptr, 1);
        memcpy(curr_frame + y * row_stride, buffer, row_stride);
    }
    free(buffer);

    // 初始化 last_frame
    static unsigned char* last_frame = NULL;
    static int last_w = 0, last_h = 0, last_components = 0;
    static int first_frame = 1;  // 首次全屏标记

    int need_full_update = 0;
    if (!last_frame || last_w != jpegW || last_h != jpegH || last_components != cinfo.output_components) {
        if (last_frame) free(last_frame);
        last_frame = (unsigned char*)malloc(jpegW * jpegH * cinfo.output_components);
        memset(last_frame, 0, jpegW * jpegH * cinfo.output_components);
        last_w = jpegW; last_h = jpegH; last_components = cinfo.output_components;
        need_full_update = 1;
    }

    if (g_AgentConfig.no_diff || first_frame) {
        need_full_update = 1;
    }

    int min_x = 0, min_y = 0, max_x = screenW - 1, max_y = screenH - 1;

    if (!need_full_update) {
        // 块级差分参数
        #define BLOCK_SIZE 16
        int blocks_x = (jpegW + BLOCK_SIZE - 1) / BLOCK_SIZE;
        int blocks_y = (jpegH + BLOCK_SIZE - 1) / BLOCK_SIZE;

        min_x = screenW; min_y = screenH;
        max_x = -1; max_y = -1;

        // 块级差分检测
        for (int by = 0; by < blocks_y; ++by) {
            for (int bx = 0; bx < blocks_x; ++bx) {
                int block_changed = 0;
                for (int y = 0; y < BLOCK_SIZE && by*BLOCK_SIZE + y < jpegH; ++y) {
                    for (int x = 0; x < BLOCK_SIZE && bx*BLOCK_SIZE + x < jpegW; ++x) {
                        int idx = ((by*BLOCK_SIZE + y) * jpegW + (bx*BLOCK_SIZE + x)) * cinfo.output_components;
                        for (int c = 0; c < cinfo.output_components; ++c) {
                            if (curr_frame[idx + c] != last_frame[idx + c]) {
                                block_changed = 1;
                                goto update_block;
                            }
                        }
                    }
                }
update_block:
                if (block_changed) {
                    int start_x = bx * BLOCK_SIZE;
                    int start_y = by * BLOCK_SIZE;
                    int end_x = (start_x + BLOCK_SIZE > jpegW) ? jpegW : (start_x + BLOCK_SIZE);
                    int end_y = (start_y + BLOCK_SIZE > jpegH) ? jpegH : (start_y + BLOCK_SIZE);

                    if (start_x < min_x) min_x = start_x;
                    if (start_y < min_y) min_y = start_y;
                    if (end_x - 1 > max_x) max_x = end_x - 1;
                    if (end_y - 1 > max_y) max_y = end_y - 1;
                }
            }
        }
        // 如果没有变化直接返回
        if (max_x < min_x || max_y < min_y) {
            free(curr_frame);
            jpeg_finish_decompress(&cinfo);
            jpeg_destroy_decompress(&cinfo);
            return;
        }
    }

    // 申请帧缓冲，一次性更新整个变化区域（首次全屏或差分）
    unsigned char* fb = (unsigned char*)request_back_vnc_buf(g_BufferManager);

    // 写入帧缓冲并填充未覆盖区域
    for (int y = 0; y < screenH; ++y) {
        for (int x = 0; x < screenW; ++x) {
            unsigned char* fb_row = &fb[y * fb_stride + x * 4];
            if (y < jpegH && x < jpegW) {
                int idx = (y * jpegW + x) * cinfo.output_components;
                fb_row[0] = curr_frame[idx + 0];
                fb_row[1] = curr_frame[idx + 1];
                fb_row[2] = curr_frame[idx + 2];
                fb_row[3] = 0xFF;
            } else {
                fb_row[0] = 255;
                fb_row[1] = 255;
                fb_row[2] = 255;
                fb_row[3] = 0xFF;
            }
        }
    }

    // release 帧缓冲一次性标记整个屏幕
    release_vnc_buf(g_BufferManager, 0, 0, screenW, screenH);

    // 更新 last_frame
    memcpy(last_frame, curr_frame, jpegW * jpegH * cinfo.output_components);
    free(curr_frame);

    first_frame = 0; // 标记首次帧已完成

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
}

static int processArguments(const int *argc, char *argv[]) {
    if (!argc) {
        return TRUE;
    }

    for (int i = 1; i < *argc;) {
        if (strcmp(argv[i], "-nodiff") == 0) {
            AGENT_OHOS_LOG(LOG_INFO, "processArguments: -nodiff");
            g_AgentConfig.no_diff = 1;
        } else if (strcmp(argv[i], "-argvtest") == 0) {
            if (i + 1 >= *argc) {
                return FALSE;
            }
            g_AgentConfig.test = strdup(argv[++i]);
        } else {
            // 未知参数, 不处理, 可能是给libvncserver的参数
        }
        i++;
    }

    return TRUE;
}

// 入口函数
RetCode UiTestExtension_OnInit(struct UiTestPort port, size_t argc, char **argv) {
    AGENT_OHOS_LOG(LOG_INFO, "UiTestExtension_OnInit: Hi~");
    g_UiTestPort = port;
    port.initLowLevelFunctions(&g_LowLevelFunctions);
    int screenW = UiTest_getScreenWidth();
    int screenH = UiTest_getScreenHeight();
    if (screenH <= 0 || screenW <= 0) {
        AGENT_OHOS_LOG(LOG_FATAL, "UiTestExtension_OnInit: Get Screen Size Failed");
        return RETCODE_FAIL;
    }
    AGENT_OHOS_LOG(LOG_FATAL, "UiTestExtension_OnInit: Screen Size: %dx%d", screenW, screenH);
    setServerRfbLog();
    int _argc = (int)argc;
    if (processArguments(&_argc, argv) == FALSE) {
        AGENT_OHOS_LOG(LOG_FATAL, "UiTestExtension_OnInit: Process Arguments Failed");
        return RETCODE_FAIL;
    }
    g_BufferManager = init_vnc_server(screenW, screenH, 32, OH_GetMarketName(), &_argc, argv);
    AGENT_OHOS_LOG(LOG_INFO, "UiTestExtension_OnInit: Bye~");
    return RETCODE_SUCCESS;
}

// 执行函数
RetCode UiTestExtension_OnRun() {
    AGENT_OHOS_LOG(LOG_INFO, "UiTestExtension_OnRun: Hi~");
    if (g_BufferManager == NULL) {
        AGENT_OHOS_LOG(LOG_FATAL, "UiTestExtension_OnRun: g_BufferManager NULL??");
        return RETCODE_FAIL;
    }
    if (UiTest_StartScreenCopy(screenJpegCallback) != 0) {
        AGENT_OHOS_LOG(LOG_FATAL, "UiTestExtension_OnRun: Start Screen Copy Failed");
        return RETCODE_FAIL;
    }
    run_vnc_server(g_BufferManager);
    if (UiTest_StopScreenCopy() != 0) {
        AGENT_OHOS_LOG(LOG_FATAL, "UiTestExtension_OnRun: Stop Screen Copy Failed");
    }
    sleep(2); // 等待2秒确保vnc服务器彻底停止
    cleanup_vnc_server(g_BufferManager);
    AGENT_OHOS_LOG(LOG_INFO, "UiTestExtension_OnRun: Bye~");
    return RETCODE_SUCCESS;
}
