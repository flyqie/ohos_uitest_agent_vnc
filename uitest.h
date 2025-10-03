#ifndef UITEST_AGENT_VNC_UITEST_H
#define UITEST_AGENT_VNC_UITEST_H

#include "agent.h"

typedef void (*ScreenCopyCallback)(char* data, int size);

extern ScreenCopyCallback g_screenCopyCallback;

enum ActionStage : uint8_t {
    ActionStage_NONE = 0,
    ActionStage_DOWN = 1,
    ActionStage_MOVE = 2,
    ActionStage_UP = 3,
    ActionStage_AXIS_UP = 4,
    ActionStage_AXIS_DOWN = 5,
    ActionStage_AXIS_STOP = 6
};

int UiTest_getScreenWidth();
int UiTest_getScreenHeight();
int UiTest_StartScreenCopy(ScreenCopyCallback cb, int mode, int fps);
int UiTest_StopScreenCopy();
int UiTest_InjectionPtr(enum ActionStage stage, int x, int y);

#endif //UITEST_AGENT_VNC_UITEST_H