#ifndef PLATFORMCONFIG_H
#define PLATOFRMCONFIG_H

#include "../../Config.h"

namespace Config
{

extern int GlobalRotation;

extern int TouchscreenMode;
extern int TouchscreenClickMode;
extern int LeftHandedMode;
extern int FastForward;

extern int ScreenRotation;
extern int ScreenGap;
extern int ScreenLayout;
extern int ScreenSwap;
extern int ScreenSizing;
extern int Filtering;
extern int upscaleFactor;
extern int IntegerScaling;
extern int ScreenAspectTop;
extern int ScreenAspectBot;

extern char LastROMPath[5][512];

extern char LastROMFolder[512];

extern int SwitchOverclock;

extern int ConsoleType;
extern int DirectBoot;

extern int LimitFramerate;

extern int ShowPerformanceMetrics;

extern char RetroAchievementsUsername[64];
extern char RetroAchievementsToken[64];

extern int hardcoreMode;

extern int notification;

// Top-screen streaming over the network (see Stream.cpp)
extern int StreamEnable;
extern char StreamHost[64];
extern int StreamPort;
extern int StreamQuality;
extern int StreamFrameSkip;
extern int StreamHideTop;
extern int StreamCodec;
extern int StreamAudio;

}

#endif