#ifndef UITEST_AGENT_VNC_UITEST_H
#define UITEST_AGENT_VNC_UITEST_H

#include "agent.h"

typedef void (*ScreenCopyCallback)(char* data, int size);

extern ScreenCopyCallback g_screenCopyCallback;

int UiTest_getScreenWidth();
int UiTest_getScreenHeight();
int UiTest_StartScreenCopy(ScreenCopyCallback cb);

#endif //UITEST_AGENT_VNC_UITEST_H