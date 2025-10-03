#include "agent.h"
#include "uitest.h"
#include <deviceinfo.h>
#include <rfb/keysym.h>
#include <jpeglib.h>
#include <png.h>

struct UiTestPort g_UiTestPort;
struct LowLevelFunctions g_LowLevelFunctions;
BufferManager* g_BufferManager;

AgentConfig g_AgentConfig = {};

void AGENT_OHOS_LOG(LogLevel level, const char* fmt, ...) {
    // 如果是调试日志且开启了调试模式, 则提升为信息日志
    // DEBUG日志在正常设备通过OH_LOG_Print打印并查看略显复杂
    if (level == LOG_DEBUG) {
        if (g_AgentConfig.agent_debug) {
            level = LOG_INFO;
        } else {
            return;
        }
    }

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
    AGENT_OHOS_LOG(LOG_DEBUG, "key_event: down=%d, key=0x%08x", down, key);

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
    AGENT_OHOS_LOG(LOG_DEBUG, "ptr_event: buttonMask=0x%02x, x=%d, y=%d", buttonMask, x, y);
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
    if (!g_BufferManager) {
        return;
    }
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
    unsigned char* buffer = (unsigned char*)malloc(row_stride);
    int y;
    static unsigned char* last_frame = NULL;
    static int last_w = 0, last_h = 0, last_components = 0;
    int need_full_update = 0;
    if (g_AgentConfig.no_diff) {
        // 禁用差分更新, 每次全帧刷新
        need_full_update = 1;
    }
    if (last_frame == NULL || last_w != jpegW || last_h != jpegH || last_components != cinfo.output_components) {
        if (last_frame) free(last_frame);
        last_frame = (unsigned char*)malloc(jpegW * jpegH * cinfo.output_components);
        memset(last_frame, 0, jpegW * jpegH * cinfo.output_components);
        last_w = jpegW;
        last_h = jpegH;
        last_components = cinfo.output_components;
        need_full_update = 1;
    }
    int min_x = jpegW, min_y = jpegH, max_x = -1, max_y = -1;
    unsigned char* curr_frame = (unsigned char*)malloc(jpegW * jpegH * cinfo.output_components);
    for (y = 0; y < jpegH; ++y) {
        unsigned char* rowptr = buffer;
        jpeg_read_scanlines(&cinfo, &rowptr, 1);
        memcpy(curr_frame + y * jpegW * cinfo.output_components, buffer, row_stride);
    }
    if (!need_full_update) {
        for (y = 0; y < jpegH; ++y) {
            for (int x = 0; x < jpegW; ++x) {
                int idx = (y * jpegW + x) * cinfo.output_components;
                int diff = 0;
                for (int c = 0; c < cinfo.output_components; ++c) {
                    if (curr_frame[idx + c] != last_frame[idx + c]) {
                        diff = 1;
                        break;
                    }
                }
                if (diff) {
                    if (x < min_x) min_x = x;
                    if (x > max_x) max_x = x;
                    if (y < min_y) min_y = y;
                    if (y > max_y) max_y = y;
                }
            }
        }
        if (max_x < min_x || max_y < min_y) {
            free(curr_frame);
            free(buffer);
            jpeg_finish_decompress(&cinfo);
            jpeg_destroy_decompress(&cinfo);
            return;
        }
    } else {
        // 全帧刷新，且以屏幕尺寸为准，防止只刷新JPEG区域
        min_x = 0; min_y = 0; max_x = screenW_local - 1; max_y = screenH_local - 1;
    }
    // 仅在有变化区域时才持有锁并写入帧缓冲
    unsigned char* fb = (unsigned char*)request_back_vnc_buf(g_BufferManager);
    // 先填充未被JPEG覆盖的区域为白色，防止黑块
    for (y = 0; y < screenH_local; ++y) {
        for (int x = 0; x < screenW_local; ++x) {
            if (y >= jpegH || x >= jpegW) {
                unsigned char* fb_row = &fb[y * fb_stride + x * 4];
                fb_row[0] = 255; fb_row[1] = 255; fb_row[2] = 255; fb_row[3] = 0xFF;
            }
        }
    }
    // 写入变化区域到帧缓冲（只写JPEG区域）
    for (y = min_y; y <= max_y && y < jpegH && y < screenH_local; ++y) {
        for (int x = min_x; x <= max_x && x < jpegW && x < screenW_local; ++x) {
            int idx = (y * jpegW + x) * cinfo.output_components;
            unsigned char r = curr_frame[idx + 0];
            unsigned char g = curr_frame[idx + 1];
            unsigned char b = curr_frame[idx + 2];
            unsigned char* fb_row = &fb[y * fb_stride + x * 4];
            fb_row[0] = r;
            fb_row[1] = g;
            fb_row[2] = b;
            fb_row[3] = 0xFF;
        }
    }
    // release_vnc_buf参数校正，防止越界
    int rel_min_x = min_x < 0 ? 0 : min_x;
    int rel_min_y = min_y < 0 ? 0 : min_y;
    int rel_max_x = max_x + 1 > screenW_local ? screenW_local : max_x + 1;
    int rel_max_y = max_y + 1 > screenH_local ? screenH_local : max_y + 1;
    release_vnc_buf(g_BufferManager, rel_min_x, rel_min_y, rel_max_x, rel_max_y);
    memcpy(last_frame, curr_frame, jpegW * jpegH * cinfo.output_components);
    free(curr_frame);
    free(buffer);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
}

// HUMAN NOTE: OHOS相关兼容接口只提供了 PNG 格式的屏幕数据, 性能较差, 没办法优化...
// AI CODE
void screenPngCallback(char* data, int size) {
    if (!g_BufferManager) return;

    int screenW_local = g_BufferManager->server->width;
    int screenH_local = g_BufferManager->server->height;

    // 使用 libpng 解码 PNG 内存数据
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    if (!png_image_begin_read_from_memory(&image, data, size)) {
        AGENT_OHOS_LOG(LOG_ERROR, LOG_TAG, "PNG decode failed");
        return;
    }

    image.format = PNG_FORMAT_RGB; // 解码成 RGB
    int pngW = image.width;
    int pngH = image.height;
    int components = 3;
    png_bytep curr_frame = (png_bytep)malloc(PNG_IMAGE_SIZE(image));
    if (!curr_frame) {
        png_image_free(&image);
        return;
    }

    if (!png_image_finish_read(&image, NULL, curr_frame, 0, NULL)) {
        AGENT_OHOS_LOG(LOG_ERROR, LOG_TAG, "PNG finish read failed");
        free(curr_frame);
        png_image_free(&image);
        return;
    }

    // 差分更新逻辑
    static png_bytep last_frame = NULL;
    static int last_w = 0, last_h = 0, last_components = 0;
    int need_full_update = 0;

    if (g_AgentConfig.no_diff) need_full_update = 1;

    if (!last_frame || last_w != pngW || last_h != pngH || last_components != components) {
        if (last_frame) free(last_frame);
        last_frame = (png_bytep)malloc(pngW * pngH * components);
        memset(last_frame, 0, pngW * pngH * components);
        last_w = pngW;
        last_h = pngH;
        last_components = components;
        need_full_update = 1;
    }

    int min_x = pngW, min_y = pngH, max_x = -1, max_y = -1;

    if (!need_full_update) {
        for (int y = 0; y < pngH; ++y) {
            for (int x = 0; x < pngW; ++x) {
                int idx = (y * pngW + x) * components;
                int diff = 0;
                for (int c = 0; c < components; ++c) {
                    if (curr_frame[idx + c] != last_frame[idx + c]) {
                        diff = 1;
                        break;
                    }
                }
                if (diff) {
                    if (x < min_x) min_x = x;
                    if (x > max_x) max_x = x;
                    if (y < min_y) min_y = y;
                    if (y > max_y) max_y = y;
                }
            }
        }

        if (max_x < min_x || max_y < min_y) {
            free(curr_frame);
            png_image_free(&image);
            return; // 没有变化
        }
    } else {
        min_x = 0; min_y = 0;
        max_x = screenW_local - 1;
        max_y = screenH_local - 1;
    }

    // 写入帧缓冲
    unsigned char* fb = (unsigned char*)request_back_vnc_buf(g_BufferManager);
    int fb_stride = screenW_local * 4;

    // 填充未被PNG覆盖的区域为白色
    for (int y = 0; y < screenH_local; ++y) {
        for (int x = 0; x < screenW_local; ++x) {
            if (y >= pngH || x >= pngW) {
                unsigned char* fb_row = &fb[y * fb_stride + x * 4];
                fb_row[0] = 255; fb_row[1] = 255; fb_row[2] = 255; fb_row[3] = 0xFF;
            }
        }
    }

    // 写入变化区域
    for (int y = min_y; y <= max_y && y < pngH && y < screenH_local; ++y) {
        for (int x = min_x; x <= max_x && x < pngW && x < screenW_local; ++x) {
            int idx = (y * pngW + x) * components;
            unsigned char* fb_row = &fb[y * fb_stride + x * 4];
            fb_row[0] = curr_frame[idx + 0];
            fb_row[1] = curr_frame[idx + 1];
            fb_row[2] = curr_frame[idx + 2];
            fb_row[3] = 0xFF;
        }
    }

    // release_vnc_buf 参数校正
    int rel_min_x = min_x < 0 ? 0 : min_x;
    int rel_min_y = min_y < 0 ? 0 : min_y;
    int rel_max_x = max_x + 1 > screenW_local ? screenW_local : max_x + 1;
    int rel_max_y = max_y + 1 > screenH_local ? screenH_local : max_y + 1;
    release_vnc_buf(g_BufferManager, rel_min_x, rel_min_y, rel_max_x, rel_max_y);

    // 更新 last_frame
    memcpy(last_frame, curr_frame, pngW * pngH * components);

    free(curr_frame);
    png_image_free(&image);
}

static int processArguments(const int *argc, char *argv[]) {
    if (!argc) {
        return TRUE;
    }

    for (int i = 1; i < *argc;) {
        if (strcmp(argv[i], "-nodiff") == 0) {
            AGENT_OHOS_LOG(LOG_INFO, "processArguments: -nodiff");
            g_AgentConfig.no_diff = 1;
        }else if (strcmp(argv[i], "-pngcap") == 0) {
            AGENT_OHOS_LOG(LOG_INFO, "processArguments: -pngcap");
            g_AgentConfig.png_cap = 1;
        }else if (strcmp(argv[i], "-agentdebug") == 0) {
            AGENT_OHOS_LOG(LOG_INFO, "processArguments: -agentdebug");
            g_AgentConfig.agent_debug = 1;
        } else if (strcmp(argv[i], "-capfps") == 0) {
            AGENT_OHOS_LOG(LOG_INFO, "processArguments: -capfps");
            if (i + 1 >= *argc) {
                return FALSE;
            }
            g_AgentConfig.cap_fps = atoi(argv[++i]);
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
    if (g_AgentConfig.cap_fps <= 0) {
        // 默认30fps
        g_AgentConfig.cap_fps = 30;
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
    AGENT_OHOS_LOG(LOG_INFO, "UiTestExtension_OnRun: max fps: %d", g_AgentConfig.cap_fps);
    int copy_ret;
    if (g_AgentConfig.png_cap) {
        copy_ret = UiTest_StartScreenCopy(screenPngCallback, 1, g_AgentConfig.cap_fps);
    } else {
        copy_ret = UiTest_StartScreenCopy(screenJpegCallback, 0, g_AgentConfig.cap_fps);
    }
    if (copy_ret != 0) {
        AGENT_OHOS_LOG(LOG_FATAL, "UiTestExtension_OnRun: Start Screen Copy Failed");
        return RETCODE_FAIL;
    }
    run_vnc_server(g_BufferManager);
    if (UiTest_StopScreenCopy() != 0) {
        AGENT_OHOS_LOG(LOG_FATAL, "UiTestExtension_OnRun: Stop Screen Copy Failed");
    }
    // 等待2秒确保vnc服务器彻底停止
    sleep(2);
    cleanup_vnc_server(g_BufferManager);
    AGENT_OHOS_LOG(LOG_INFO, "UiTestExtension_OnRun: Bye~");
    return RETCODE_SUCCESS;
}
