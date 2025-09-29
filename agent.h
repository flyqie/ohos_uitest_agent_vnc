#ifndef UITEST_AGENT_VNC_LIBRARY_H
#define UITEST_AGENT_VNC_LIBRARY_H

#include <stdio.h>
#include <hilog/log.h>
#include <rfb/rfb.h>
#include "third/ohos/extension_c_api.h"

extern struct UiTestPort g_UiTestPort;
extern struct LowLevelFunctions g_LowLevelFunctions;
extern rfbScreenInfoPtr g_rfbServer;

// 入口函数
RetCode UiTestExtension_OnInit(struct UiTestPort port, size_t argc, char **argv);
// 执行函数
RetCode UiTestExtension_OnRun();

#endif // UITEST_AGENT_VNC_LIBRARY_H