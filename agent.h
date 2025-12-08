#ifndef UITEST_AGENT_VNC_LIBRARY_H
#define UITEST_AGENT_VNC_LIBRARY_H

#include <stdio.h>
#include <unistd.h>
#include <hilog/log.h>
#include <rfb/rfb.h>
#include <rfb/keysym.h>
#include <ohos/extension_c_api.h>

typedef struct {
    rfbScreenInfoPtr server;
    char *frontBuffer;
    char *backBuffer;
    int bufferSize;
    int stop_vnc_server_flag;
    int stopped_vnc_server_flag;
    int have_client_flag;
    pthread_rwlock_t frontBufferLock;
    pthread_mutex_t backBufferLock;
    pthread_mutex_t backBufferFuncLock;
} BufferManager;

#define CAP_MODE_PNG "png"
#define CAP_MODE_DMPUB "dmpub"
#define CAP_MODE_DEFAULT "jpeg"
typedef struct {
    bool no_diff;
    char cap_mode[16];
    int cap_fps;
    bool agent_debug;
} AgentConfig;

extern struct UiTestPort g_UiTestPort;
extern struct LowLevelFunctions g_LowLevelFunctions;
extern BufferManager* g_BufferManager;
extern AgentConfig g_AgentConfig;

RetCode UiTestExtension_OnInit(struct UiTestPort port, size_t argc, char **argv);
RetCode UiTestExtension_OnRun();

void AGENT_OHOS_LOG(LogLevel level, const char* fmt, ...);

#endif // UITEST_AGENT_VNC_LIBRARY_H