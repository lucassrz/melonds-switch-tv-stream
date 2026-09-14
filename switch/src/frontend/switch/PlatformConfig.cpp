#include "PlatformConfig.h"

namespace Config
{

int GlobalRotation;

int TouchscreenMode;
int TouchscreenClickMode;
int LeftHandedMode;
int FastForward;

int ScreenRotation;
int ScreenGap;
int ScreenLayout;
int ScreenSwap;
int ScreenSizing;
int Filtering;
int upscaleFactor;
int IntegerScaling;
int ScreenAspectTop;
int ScreenAspectBot;

int SavestateRelocSRAM;

char LastROMFolder[512];

char LastROMPath[5][512];

int SwitchOverclock;

int ConsoleType;
int DirectBoot;

int LimitFramerate;

int ShowPerformanceMetrics;

char RetroAchievementsUsername[64] = "";
char RetroAchievementsToken[64] = "";

int hardcoreMode = 0;

int notification = 0;

int StreamEnable = 0;
char StreamHost[64] = "";
int StreamPort = 9797;
int StreamQuality = 92;
int StreamFrameSkip = 1;
int StreamHideTop = 1;
int StreamCodec = 2;   // 0 = JPEG, 1 = lossless (QOI), 2 = auto (lossless when small)
int StreamAudio = 0;   // 0 = Switch speakers, 1 = TV

ConfigEntry PlatformConfigFile[] =
{
    {"GlobalRotation",          0, &GlobalRotation,         0, NULL, 0},
    
    {"TouchscreenMode",         0, &TouchscreenMode,        0, NULL, 0},
    {"TouchscreenClickMode",    0, &TouchscreenClickMode,   0, NULL, 0},
    {"LeftHandedMode",          0, &LeftHandedMode,         0, NULL, 0},
    {"FastForward",             0, &FastForward,            0, NULL, 0},

    {"SavStaRelocSRAM",         0, &SavestateRelocSRAM,     0, NULL, 0},

    {"ScreenRotation",          0, &ScreenRotation,         0, NULL, 0},
    {"ScreenGap",               0, &ScreenGap,              0, NULL, 0},
    {"ScreenLayout",            0, &ScreenLayout,           0, NULL, 0},
    {"ScreenSwap",              0, &ScreenSwap,             0, NULL, 0},
    {"ScreenSizing",            0, &ScreenSizing,           0, NULL, 0},
    {"Filtering",               0, &Filtering,              1, NULL, 0},
    {"upscaleFactor",           0, &upscaleFactor,          1, NULL, 0},
    {"IntegerScaling",          0, &IntegerScaling,         0, NULL, 0},
    {"ScreenAspectTop",         0, &ScreenAspectTop,        0, NULL, 0},
    {"ScreenAspectBot",         0, &ScreenAspectBot,        0, NULL, 0},

    {"LastROMPath0",            1, LastROMPath[0],          0, "",   511},
    {"LastROMPath1",            1, LastROMPath[1],          0, "",   511},
    {"LastROMPath2",            1, LastROMPath[2],          0, "",   511},
    {"LastROMPath3",            1, LastROMPath[3],          0, "",   511},
    {"LastROMPath4",            1, LastROMPath[4],          0, "",   511},

    {"LastROMFolder",           1, LastROMFolder,           0, "/", 511},

    {"SwitchOverclock",         0, &SwitchOverclock,        3, NULL, 0}, // 1785 MHz: needed for 3D games

    {"ConsoleType",             0, &ConsoleType,            0, NULL, 0},
    {"DirectBoot",              0, &DirectBoot,             1, NULL, 0},

    {"ShowPerformanceMetrics",  0, &ShowPerformanceMetrics, 0, NULL, 0},

    {"LimitFramerate",          0, &LimitFramerate,         1, NULL, 0},

    {"RetroAchievementsUsername",   1, RetroAchievementsUsername,          0, "\0",   63},
    {"RetroAchievementsToken",      1, RetroAchievementsToken,             0, "\0",   63},

    {"StreamEnable",            0, &StreamEnable,           0, NULL, 0},
    {"StreamHost",              1, StreamHost,              0, "",   63},
    {"StreamPort",              0, &StreamPort,          9797, NULL, 0},
    {"StreamQuality",           0, &StreamQuality,         92, NULL, 0},
    {"StreamFrameSkip",         0, &StreamFrameSkip,        1, NULL, 0},
    {"StreamHideTop",           0, &StreamHideTop,          1, NULL, 0},
    {"StreamCodec",             0, &StreamCodec,            2, NULL, 0},
    {"StreamAudio",             0, &StreamAudio,            0, NULL, 0},

    {"hardcoreMode",                0, &hardcoreMode,                      0, NULL,    0},

    {"notification",                0, &notification,                      0, NULL,    0},

    {"", -1, NULL, 0, NULL, 0}
};

}