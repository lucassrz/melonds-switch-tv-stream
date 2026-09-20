#include "SettingsDialog.h"

#include "Style.h"
#include "KeyExplanations.h"
#include "BackButton.h"
#include "Stream.h"
#include "main.h"

#include <switch.h>

#include "PlatformConfig.h"
#include "InputConfig.h"

#include <string.h>
#include <stdio.h>

#include "RetroAchievements.h"
#include "NotificationSystem.h"

namespace {
    static u64 PlatformKeysHeld = 0;
    static u64 PlatformKeysDown = 0;
    static u64 PreviousKeys = 0;
}

static PadState pad;

namespace SettingsDialog
{
const char* SettingsPrefix = "settingsdialog_entries";

const char* ComboboxElementPrefix = "settings_combobox";

const int MaxDiscoveredOptions = 8;

// ---- row helpers -------------------------------------------------------------

static BoxGui::Frame MakeRow(BoxGui::Frame& parent, BoxGui::Skewer& skewer)
{
    return BoxGui::Frame{parent, skewer.Spit({parent.Area.Size.X, UIRowHeight + UIRowGap}, Gfx::align_Right),
        {0.f, UIRowGap / 2.f}, {0.f, UIRowGap / 2.f}};
}

static void DrawRowChrome(BoxGui::Frame& row, bool selected)
{
    if (!selected)
        return;
    Gfx::DrawRoundedRect(row.Area.Position, row.Area.Size, RaisedColor, UIRadius);
    Gfx::DrawRoundedOutline(row.Area.Position, row.Area.Size, AccentColor, UIRadius, 2.f);
}

static void DrawRowLabel(BoxGui::Frame& row, const char* name)
{
    Gfx::DrawText(Gfx::SystemFontStandard, row.Area.Position + Gfx::Vector2f{16.f, row.Area.Size.Y / 2.f},
        TextLineHeight, TextColor, Gfx::align_Left, Gfx::align_Center, name);
}

static Gfx::Vector2f RowRight(BoxGui::Frame& row, float inset = 16.f)
{
    return row.Area.Position + Gfx::Vector2f{row.Area.Size.X - inset, row.Area.Size.Y / 2.f};
}

static void DrawToggle(Gfx::Vector2f rightCenter, bool on)
{
    const float w = 44.f, h = 26.f;
    Gfx::Vector2f pos = rightCenter - Gfx::Vector2f{w, h / 2.f};
    Gfx::DrawRoundedRect(pos, {w, h}, on ? AccentColor : LineColor, h / 2.f);
    Gfx::DrawCircle(pos + Gfx::Vector2f{on ? w - h / 2.f : h / 2.f, h / 2.f}, 10.f, on ? BgColor : TextMutedColor);
    Gfx::DrawText(Gfx::SystemFontStandard, pos - Gfx::Vector2f{12.f, -h / 2.f}, TextLineHeight * 0.8f, TextMutedColor,
        Gfx::align_Right, Gfx::align_Center, on ? "On" : "Off");
}

static int CountOptions(const char* options)
{
    int n = 0;
    while (*options)
    {
        n++;
        options += strlen(options) + 1;
    }
    return n;
}

static const char* OptionName(const char* options, int index)
{
    for (int i = 0; i < index; i++)
    {
        options += strlen(options) + 1;
        if (*options == '\0')
            return "?";
    }
    return options;
}

// Dark overlay plus a centered card; returns the card frame area.
static BoxGui::Rect ModalCard(BoxGui::Frame& rootFrame, double startTimestamp, float height)
{
    Gfx::Color color = OverlayColor;
    color.A = (float)std::min((Gfx::AnimationTimestamp - startTimestamp) * 5.0, 0.8);
    Gfx::DrawRectangle(rootFrame.Area.Position, rootFrame.Area.Size, color);
    Gfx::Vector2f size = {std::min(rootFrame.Area.Size.X * 0.9f, 720.f), std::min(rootFrame.Area.Size.Y * 0.9f, height)};
    BoxGui::Rect rect = rootFrame.Area.CenteredChild(size);
    Gfx::DrawRoundedRect(rect.Position, rect.Size, CardColor, UIRadiusLarge);
    Gfx::DrawRoundedOutline(rect.Position, rect.Size, BorderColor, UIRadiusLarge, 1.f);
    return rect;
}

// ---- widgets -----------------------------------------------------------------

void DoSlider(BoxGui::Frame& parent, BoxGui::Skewer& skewer, const char* name, int& value, int low, int high, bool first = false)
{
    BoxGui::Frame row = MakeRow(parent, skewer);
    bool selected = BoxGui::InputElement(row, BoxGui::MakeUniqueName(SettingsPrefix, BoxGui::MakeUniqueName(name, 0)), first);

    if (selected && BoxGui::LeftPressed())
        value = std::max(value - 1, low);
    if (selected && BoxGui::RightPressed())
        value = std::min(value + 1, high);

    DrawRowChrome(row, selected);
    DrawRowLabel(row, name);

    const float trackWidth = 180.f, trackHeight = 6.f;
    Gfx::Vector2f right = RowRight(row);
    char valueText[16];
    snprintf(valueText, sizeof(valueText), "%d", value);
    Gfx::DrawText(Gfx::SystemFontStandard, right, TextLineHeight, TextSoftColor, Gfx::align_Right, Gfx::align_Center, valueText);
    Gfx::Vector2f trackPos = right - Gfx::Vector2f{56.f + trackWidth, trackHeight / 2.f};
    float ratio = (float)(value - low) / (float)std::max(high - low, 1);
    Gfx::DrawRoundedRect(trackPos, {trackWidth, trackHeight}, LineColor, trackHeight / 2.f);
    Gfx::DrawRoundedRect(trackPos, {std::max(trackWidth * ratio, trackHeight), trackHeight}, AccentColor, trackHeight / 2.f);
    Gfx::DrawCircle(trackPos + Gfx::Vector2f{trackWidth * ratio, trackHeight / 2.f}, 9.f, selected ? TextColor : TextSoftColor);
}

void DoCheckbox(BoxGui::Frame& parent, BoxGui::Skewer& skewer, const char* name, bool& value, bool first = false)
{
    BoxGui::Frame row = MakeRow(parent, skewer);
    bool selected = BoxGui::InputElement(row, BoxGui::MakeUniqueName(SettingsPrefix, BoxGui::MakeUniqueName(name, 0)), first);

    if (selected && BoxGui::ConfirmPressed())
        value ^= true;
    if (selected)
        KeyExplanation::Explain(KeyExplanation::button_A, "Toggle");

    DrawRowChrome(row, selected);
    DrawRowLabel(row, name);
    DrawToggle(RowRight(row), value);
}

void DoCombobox(BoxGui::Frame& parent, BoxGui::Skewer& skewer, const char* name, const char* options, int& selectedOption, bool first = false)
{
    BoxGui::Frame row = MakeRow(parent, skewer);
    bool selected = BoxGui::InputElement(row, BoxGui::MakeUniqueName(SettingsPrefix, BoxGui::MakeUniqueName(name, 0)), first);

    int count = CountOptions(options);
    if (count == 0)
        return;
    if (selectedOption >= count || selectedOption < 0)
        selectedOption = 0;

    if (selected && BoxGui::LeftPressed())
        selectedOption = (selectedOption + count - 1) % count;
    if (selected && BoxGui::RightPressed())
        selectedOption = (selectedOption + 1) % count;

    if (selected && BoxGui::ConfirmPressed())
    {
        struct Dialog
        {
            int OriginalValue;
            int& SelectedOption;
            const char* Name, *Options;
            double StartTimestamp;
            double EndTimestamp = -INFINITY;

            bool operator()(BoxGui::Frame& rootFrame)
            {
                const float pad = 24.f, titleHeight = 56.f;
                int count = CountOptions(Options);
                BoxGui::Rect card = ModalCard(rootFrame, StartTimestamp, pad + titleHeight + count * (UIRowHeight + UIRowGap) + pad);
                BoxGui::Frame dialogFrame{rootFrame, card};

                Gfx::DrawText(Gfx::SystemFontStandard, dialogFrame.Area.Position + Gfx::Vector2f{pad + 16.f, pad + titleHeight / 2.f},
                    TextLineHeight * 1.4f, TextColor, Gfx::align_Left, Gfx::align_Center, Name);

                BoxGui::Frame optionsFrame{dialogFrame,
                    {{pad, pad + titleHeight}, {dialogFrame.Area.Size.X - 2.f * pad, dialogFrame.Area.Size.Y - 2.f * pad - titleHeight}},
                    {0.f, 0.f}, {0.f, 0.f},
                    BoxGui::direction_Vertical, BoxGui::MakeUniqueName(ComboboxElementPrefix, -1), false, true};
                Gfx::PushScissor(optionsFrame.Area.Position.X, optionsFrame.Area.Position.Y, optionsFrame.Area.Size.X, optionsFrame.Area.Size.Y);
                BoxGui::Skewer optionsSkewer{optionsFrame, 0.f, BoxGui::direction_Vertical};

                for (int i = 0; i < count; i++)
                {
                    BoxGui::Frame optionFrame = MakeRow(optionsFrame, optionsSkewer);
                    bool optionSelected = BoxGui::InputElement(optionFrame, BoxGui::MakeUniqueName(ComboboxElementPrefix, i));
                    if (optionSelected && BoxGui::ConfirmPressed() && EndTimestamp < 0.0)
                    {
                        SelectedOption = i;
                        EndTimestamp = SelectedOption != OriginalValue ? Gfx::AnimationTimestamp : 0.0;
                    }
                    if (!optionFrame.IsVisible())
                        continue;
                    DrawRowChrome(optionFrame, optionSelected);
                    if (SelectedOption == i)
                        Gfx::DrawText(Gfx::SystemFontNintendoExt, optionFrame.Area.Position + Gfx::Vector2f{16.f, optionFrame.Area.Size.Y / 2.f},
                            TextLineHeight, AccentColor, Gfx::align_Left, Gfx::align_Center, GFX_NINTENDOFONT_CHECKMARK);
                    Gfx::DrawText(Gfx::SystemFontStandard, optionFrame.Area.Position + Gfx::Vector2f{48.f, optionFrame.Area.Size.Y / 2.f},
                        TextLineHeight, TextColor, Gfx::align_Left, Gfx::align_Center, OptionName(Options, i));
                    if (optionSelected)
                        KeyExplanation::Explain(KeyExplanation::button_A, "Choose");
                }
                Gfx::PopScissor();

                KeyExplanation::Explain(KeyExplanation::button_B, "Cancel");
                if (BoxGui::CancelPressed())
                    EndTimestamp = 0.0;
                KeyExplanation::DoGui(rootFrame);

                // don't close the dialog immediately
                // instead wait a moment so the user can reflect on their choice :D
                const double fadeoutLength = 0.25;
                return EndTimestamp < 0.0 || Gfx::AnimationTimestamp - EndTimestamp < fadeoutLength;
            }
        };
        BoxGui::OpenModalDialog(Dialog{selectedOption, selectedOption, name, options, Gfx::AnimationTimestamp});
        BoxGui::ForceSelecton(BoxGui::MakeUniqueName(ComboboxElementPrefix, selectedOption), true, 1);
    }

    if (selected)
        KeyExplanation::Explain(KeyExplanation::button_A, "Choose");

    DrawRowChrome(row, selected);
    DrawRowLabel(row, name);

    Gfx::Vector2f right = RowRight(row);
    if (selected)
    {
        Gfx::DrawText(Gfx::SystemFontStandard, right, TextLineHeight, TextMutedColor, Gfx::align_Right, Gfx::align_Center, ">");
        Gfx::Vector2f valueSize = Gfx::MeasureText(Gfx::SystemFontStandard, TextLineHeight, OptionName(options, selectedOption));
        Gfx::DrawText(Gfx::SystemFontStandard, right - Gfx::Vector2f{18.f, 0.f}, TextLineHeight, TextColor,
            Gfx::align_Right, Gfx::align_Center, OptionName(options, selectedOption));
        Gfx::DrawText(Gfx::SystemFontStandard, right - Gfx::Vector2f{18.f + valueSize.X + 8.f, 0.f}, TextLineHeight, TextMutedColor,
            Gfx::align_Right, Gfx::align_Center, "<");
    }
    else
    {
        Gfx::DrawText(Gfx::SystemFontStandard, right, TextLineHeight, TextSoftColor,
            Gfx::align_Right, Gfx::align_Center, OptionName(options, selectedOption));
    }
}

void SectionHeader(BoxGui::Frame& parent, BoxGui::Skewer& skewer, const char* name)
{
    skewer.Advance(22.f);
    BoxGui::Frame nameFrame{parent, skewer.Spit({parent.Area.Size.X, 30.f}, Gfx::align_Right)};
    Gfx::DrawText(Gfx::SystemFontStandard, nameFrame.Area.Position + Gfx::Vector2f{16.f, 12.f}, TextLineHeight * 0.75f, TextMutedColor,
        Gfx::align_Left, Gfx::align_Center, name);
    Gfx::DrawRectangle(nameFrame.Area.Position + Gfx::Vector2f{0.f, 29.f}, {nameFrame.Area.Size.X, 1.f}, BorderColor);
    skewer.Advance(6.f);
}

void DoTextField(BoxGui::Frame& parent, BoxGui::Skewer& skewer, const char* label, char* buffer, size_t bufferSize, bool first = false)
{
    BoxGui::Frame row = MakeRow(parent, skewer);
    bool selected = BoxGui::InputElement(row, BoxGui::MakeUniqueName(SettingsPrefix, BoxGui::MakeUniqueName(label, 0)), first);

    if (selected && BoxGui::ConfirmPressed())
    {
        SwkbdConfig kbd;
        Result rc = swkbdCreate(&kbd, 0);
        if (R_SUCCEEDED(rc)) {
            swkbdConfigMakePresetDefault(&kbd);
            swkbdConfigSetInitialText(&kbd, buffer);
            swkbdConfigSetTextCheckCallback(&kbd, NULL);

            char out[bufferSize];
            rc = swkbdShow(&kbd, out, bufferSize);
            if (R_SUCCEEDED(rc)) {
                strncpy(buffer, out, bufferSize - 1);
                buffer[bufferSize - 1] = '\0';
            }
            swkbdClose(&kbd);
        }
    }
    if (selected)
        KeyExplanation::Explain(KeyExplanation::button_A, "Edit");

    DrawRowChrome(row, selected);
    DrawRowLabel(row, label);
    Gfx::DrawText(Gfx::SystemFontStandard, RowRight(row), TextLineHeight, buffer[0] ? TextSoftColor : TextMutedColor,
        Gfx::align_Right, Gfx::align_Center, buffer[0] ? buffer : "Tap to enter");
}

void DoLabel(BoxGui::Frame& parent, BoxGui::Skewer& skewer, const char* text, bool first = false)
{
    BoxGui::Frame row = MakeRow(parent, skewer);
    bool selected = BoxGui::InputElement(row, BoxGui::MakeUniqueName(SettingsPrefix, BoxGui::MakeUniqueName(text, 0)), first);
    DrawRowChrome(row, selected);
    Gfx::DrawText(Gfx::SystemFontStandard, row.Area.Position + Gfx::Vector2f{16.f, row.Area.Size.Y / 2.f},
        TextLineHeight, TextSoftColor, Gfx::align_Left, Gfx::align_Center, text);
}

const char* ButtonToString(u64 buttons)
{
    static std::string result;
    result.clear();
    bool first = true;

    if (buttons & HidNpadButton_A) {
        if (!first) result += " + ";
        result += "A";
        first = false;
    }
    if (buttons & HidNpadButton_B) {
        if (!first) result += " + ";
        result += "B";
        first = false;
    }
    if (buttons & HidNpadButton_X) {
        if (!first) result += " + ";
        result += "X";
        first = false;
    }
    if (buttons & HidNpadButton_Y) {
        if (!first) result += " + ";
        result += "Y";
        first = false;
    }
    if (buttons & HidNpadButton_StickL) {
        if (!first) result += " + ";
        result += "L Stick Button";
        first = false;
    }
    if (buttons & HidNpadButton_StickR) {
        if (!first) result += " + ";
        result += "R Stick Button";
        first = false;
    }
    if (buttons & HidNpadButton_L) {
        if (!first) result += " + ";
        result += "L";
        first = false;
    }
    if (buttons & HidNpadButton_R) {
        if (!first) result += " + ";
        result += "R";
        first = false;
    }
    if (buttons & HidNpadButton_ZL) {
        if (!first) result += " + ";
        result += "ZL";
        first = false;
    }
    if (buttons & HidNpadButton_ZR) {
        if (!first) result += " + ";
        result += "ZR";
        first = false;
    }
    if (buttons & HidNpadButton_Plus) {
        if (!first) result += " AND ";
        result += "+";
        first = false;
    }
    if (buttons & HidNpadButton_Minus) {
        if (!first) result += " + ";
        result += "-";
        first = false;
    }
    if (buttons & HidNpadButton_Up) {
        if (!first) result += " + ";
        result += "D-Pad UP";
        first = false;
    }
    if (buttons & HidNpadButton_Down) {
        if (!first) result += " + ";
        result += "D-Pad Down";
        first = false;
    }
    if (buttons & HidNpadButton_Right) {
        if (!first) result += " + ";
        result += "D-Pad Right";
        first = false;
    }
    if (buttons & HidNpadButton_Left) {
        if (!first) result += " + ";
        result += "D-Pad Left";
        first = false;
    }
    if (buttons & HidNpadButton_StickLLeft) {
        if (!first) result += " + ";
        result += "L Stick Left";
        first = false;
    }
    if (buttons & HidNpadButton_StickLUp) {
        if (!first) result += " + ";
        result += "L Stick Up";
        first = false;
    }
    if (buttons & HidNpadButton_StickLRight) {
        if (!first) result += " + ";
        result += "L Stick Right";
        first = false;
    }
    if (buttons & HidNpadButton_StickLDown) {
        if (!first) result += " + ";
        result += "L Stick Down";
        first = false;
    }
    if (buttons & HidNpadButton_StickRLeft) {
        if (!first) result += " + ";
        result += "R Stick Left";
        first = false;
    }
    if (buttons & HidNpadButton_StickRUp) {
        if (!first) result += " + ";
        result += "R Stick Up";
        first = false;
    }
    if (buttons & HidNpadButton_StickRRight) {
        if (!first) result += " + ";
        result += "R Stick Right";
        first = false;
    }
    if (buttons & HidNpadButton_StickRDown) {
        if (!first) result += " + ";
        result += "R Stick Down";
        first = false;
    }
    if (buttons & HidNpadButton_LeftSL) {
        if (!first) result += " + ";
        result += "Left SL";
        first = false;
    }
    if (buttons & HidNpadButton_LeftSR) {
        if (!first) result += " + ";
        result += "Left SR";
        first = false;
    }
    if (buttons & HidNpadButton_RightSL) {
        if (!first) result += " + ";
        result += "Right SL";
        first = false;
    }
    if (buttons & HidNpadButton_RightSR) {
        if (!first) result += " + ";
        result += "Right SR";
        first = false;
    }

    if (result.empty()) {
        return "•͡˘㇁•͡˘";
    }

    return result.c_str();
}


void DoInputButton(BoxGui::Frame& parent, BoxGui::Skewer& skewer, const char* name, u64& mappedKey, bool first = false)
{
    BoxGui::Frame row = MakeRow(parent, skewer);
    bool selected = BoxGui::InputElement(row, BoxGui::MakeUniqueName(SettingsPrefix, BoxGui::MakeUniqueName(name, 0)), first);

    if (selected && BoxGui::ConfirmPressed())
    {
        struct Dialog
        {
            const char* Name;
            u64& MappedKey;
            double StartTimestamp;
            double EndTimestamp = -INFINITY;
            bool inputCaptured = false;

            bool operator()(BoxGui::Frame& rootFrame)
            {
                const float pad = 28.f;
                BoxGui::Rect card = ModalCard(rootFrame, StartTimestamp, 200.f);
                Gfx::DrawText(Gfx::SystemFontStandard, card.Position + Gfx::Vector2f{pad, pad + 14.f},
                    TextLineHeight * 1.4f, TextColor, Gfx::align_Left, Gfx::align_Center, Name);
                Gfx::DrawText(Gfx::SystemFontStandard, card.Position + Gfx::Vector2f{pad, pad + 64.f},
                    TextLineHeight, TextSoftColor, Gfx::align_Left, Gfx::align_Center, "Press the button to map...");
                Gfx::DrawText(Gfx::SystemFontStandard, card.Position + Gfx::Vector2f{pad, pad + 94.f},
                    TextLineHeight * 0.85f, TextMutedColor, Gfx::align_Left, Gfx::align_Center, "Waits 10 seconds, B cancels.");

                const double elapsedInput = Gfx::AnimationTimestamp - StartTimestamp;
                if (!inputCaptured && elapsedInput > 0.3)
                {
                    static PadState pad;
                    static bool padInitialized = false;
                    if (!padInitialized) {
                        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
                        padInitializeAny(&pad);
                        padInitialized = true;
                    }
                    padUpdate(&pad);
                    u64 keys = padGetButtonsDown(&pad);
                    if (keys != 0) {
                        MappedKey = keys;
                        inputCaptured = true;
                        EndTimestamp = Gfx::AnimationTimestamp;
                    }
                }

                const double elapsed = Gfx::AnimationTimestamp - StartTimestamp;
                if (elapsed > 10.0 || BoxGui::CancelPressed())
                    EndTimestamp = 0.f;

                KeyExplanation::Explain(KeyExplanation::button_B, "Cancel");
                KeyExplanation::DoGui(rootFrame);

                const double fadeoutLength = 0.25;
                return EndTimestamp < 0.0 || Gfx::AnimationTimestamp - EndTimestamp < fadeoutLength;
            }
        };
        BoxGui::OpenModalDialog(Dialog{name, mappedKey, Gfx::AnimationTimestamp});
    }
    if (selected)
        KeyExplanation::Explain(KeyExplanation::button_A, "Remap");

    DrawRowChrome(row, selected);
    DrawRowLabel(row, name);
    Gfx::DrawText(Gfx::SystemFontStandard, RowRight(row), TextLineHeight, TextSoftColor,
        Gfx::align_Right, Gfx::align_Center, ButtonToString(mappedKey));
}

void ShowImage(BoxGui::Frame& parent, BoxGui::Skewer& skewer, int textureId, int nwidth, int nheight, float imageSize = 64.f)
{
    if (textureId < 0 || nwidth <= 0 || nheight <= 0)
        return;
    BoxGui::Frame row{parent, skewer.Spit({parent.Area.Size.X, imageSize + 10.f}, Gfx::align_Right), {0.f, 5.f}, {0.f, 5.f}};
    Gfx::DrawRectangle(textureId, row.Area.Position + Gfx::Vector2f{16.f, 0.f}, {imageSize, imageSize},
        {0.f, 0.f}, {static_cast<float>(nwidth), static_cast<float>(nheight)},
        Gfx::Color{1.f, 1.f, 1.f, 1.f}, false, 8.f);
}

void DoGui(BoxGui::Frame& parent)
{
    BoxGui::Frame settingsFrame{parent,
        {{0.f, BackButtonHeight}, {parent.Area.Size.X, parent.Area.Size.Y - BackButtonHeight}},
        {UIPagePadding, 8.f}, {UIPagePadding, 0.f},
        BoxGui::direction_Vertical, BoxGui::MakeUniqueName(SettingsPrefix, -1), false, true};

    BoxGui::Skewer settingsSkewer{settingsFrame, 0.f, BoxGui::direction_Vertical};

    Gfx::PushScissor(settingsFrame.Area.Position.X, settingsFrame.Area.Position.Y, settingsFrame.Area.Size.X, settingsFrame.Area.Size.Y);

    const char* title = "error";
    switch (CurrentUiScreen)
    {
    case uiScreen_EmulationSettings:
        title = "Emulation";
        {
            SectionHeader(settingsFrame, settingsSkewer, "General");
            DoCombobox(settingsFrame, settingsSkewer, "Console mode", "DS\0DSi (experimental)\0", Config::ConsoleType, true);
            if (Config::ConsoleType == 0)
            {
                bool bootDirectly = Config::DirectBoot;
                DoCheckbox(settingsFrame, settingsSkewer, "Boot directly (Skip bios)", bootDirectly);
                Config::DirectBoot = bootDirectly;
            }
            DoCombobox(settingsFrame, settingsSkewer, "Switch CPU clock", "1020 MHz\0" "1224 MHz\0" "1581 MHz\0" "1785 MHz\0" "918 Mhz\0" "816 Mhz\0" "714 Mhz\0", Config::SwitchOverclock);
        }
        {
            bool jitEnable = Config::JIT_Enable;
            SectionHeader(settingsFrame, settingsSkewer, "JIT recompiler");
            bool branchOptimisations = Config::JIT_BranchOptimisations;
            bool literalOptimisations = Config::JIT_LiteralOptimisations;
            bool fastMemory = Config::JIT_FastMemory;

            DoCheckbox(settingsFrame, settingsSkewer, "Enable JIT recompiler", jitEnable, true);
            if (jitEnable)
            {
                DoSlider(settingsFrame, settingsSkewer, "Maximum block size", Config::JIT_MaxBlockSize, 1, 32);
                DoCheckbox(settingsFrame, settingsSkewer, "Enable JIT Branch Optimisations", branchOptimisations);
                DoCheckbox(settingsFrame, settingsSkewer, "Enable JIT Literal Optimisations", literalOptimisations);
                DoCheckbox(settingsFrame, settingsSkewer, "Enable JIT Fast Memory", fastMemory);
            }

            Config::JIT_Enable = jitEnable;
            Config::JIT_BranchOptimisations = branchOptimisations;
            Config::JIT_LiteralOptimisations = literalOptimisations;
            Config::JIT_FastMemory = fastMemory;
        }
        {
            bool loginRA = false, hardcore = Config::hardcoreMode, notification = Config::notification;
            int status = 0;
            static char username[64] = {0}, password[64] = {0};

            if (strlen(Config::RetroAchievementsUsername) > 0 && strlen(username) == 0) {
                strncpy(username, Config::RetroAchievementsUsername, sizeof(username) - 1);
                username[sizeof(username) - 1] = '\0';
            }

            SectionHeader(settingsFrame, settingsSkewer, "RetroAchievements");
            DoTextField(settingsFrame, settingsSkewer, "RetroAchievements Username", username, sizeof(username));
            DoTextField(settingsFrame, settingsSkewer, "RetroAchievements Password", password, sizeof(password));
            DoCheckbox(settingsFrame, settingsSkewer, "Login", loginRA);
            if (loginRA)
                InitRetroAchievements(username, password, false);

            DoCheckbox(settingsFrame, settingsSkewer, "Hardcore Mode", hardcore);
            Config::hardcoreMode = hardcore;

            DoCheckbox(settingsFrame, settingsSkewer, "Disable RA notifications", notification);
            Config::notification = notification;
        }
        break;
    case uiScreen_DisplaySettings:
        title = "Display";
        {
            SectionHeader(settingsFrame, settingsSkewer, "Framerate");
            bool limitFramerate = Config::LimitFramerate;
            DoCheckbox(settingsFrame, settingsSkewer, "Limit framerate", limitFramerate, true);
            Config::LimitFramerate = limitFramerate;
        }
        {
            SectionHeader(settingsFrame, settingsSkewer, "GUI");

            DoCombobox(settingsFrame, settingsSkewer, "Global rotation", "0°\090°\000180°\000270°\0", Config::GlobalRotation, true);
            bool showPerformanceMetrics = Config::ShowPerformanceMetrics;
            DoCheckbox(settingsFrame, settingsSkewer, "Show performance metrics", showPerformanceMetrics);
            Config::ShowPerformanceMetrics = showPerformanceMetrics;
        }
        {
            SectionHeader(settingsFrame, settingsSkewer, "Screens");

            DoCombobox(settingsFrame, settingsSkewer, "Rotation", "0°\090°\000180°\000270°\0", Config::ScreenRotation, true);
            DoCombobox(settingsFrame, settingsSkewer, "Sizing", "Even\0Emphasise top\0Emphasise bottom\0Auto\0Top only\0Bottom only\0", Config::ScreenSizing);
            DoCombobox(settingsFrame, settingsSkewer, "Gap", "0\0001\08\00016\00032\00064\0090\000128\0", Config::ScreenGap);
            DoCombobox(settingsFrame, settingsSkewer, "Layout", "Natural\0Vertical\0Horizontal\0Hybrid\0", Config::ScreenLayout);
            DoCombobox(settingsFrame, settingsSkewer, "Aspect ratio top", "4:3 (native)\00016:9\0", Config::ScreenAspectTop);
            DoCombobox(settingsFrame, settingsSkewer, "Aspect ratio bottom", "4:3 (native)\00016:9\0", Config::ScreenAspectBot);
            bool screenSwap = Config::ScreenSwap;
            DoCheckbox(settingsFrame, settingsSkewer, "Swap screens", screenSwap);
            Config::ScreenSwap = screenSwap;
            bool integerScaling = Config::IntegerScaling;
            DoCheckbox(settingsFrame, settingsSkewer, "Integer scaling", integerScaling);
            Config::IntegerScaling = integerScaling;
            DoCombobox(settingsFrame, settingsSkewer, "Filtering", "Nearest\0Linear\0", Config::Filtering);
            DoCombobox(settingsFrame, settingsSkewer, "Upscaler (NOT WORKING)", "1x\0002x\0003x\0004x\0", Config::upscaleFactor);
        }
        {
            SectionHeader(settingsFrame, settingsSkewer, "Top screen streaming");
            bool streamEnable = Config::StreamEnable;
            DoCheckbox(settingsFrame, settingsSkewer, "Stream top screen to a TV over wifi", streamEnable);
            Config::StreamEnable = streamEnable;
            if (FocusStreamingSection)
            {
                BoxGui::ForceSelecton(BoxGui::MakeUniqueName(SettingsPrefix, BoxGui::MakeUniqueName("Stream top screen to a TV over wifi", 0)), true);
                FocusStreamingSection = false;
            }
            if (streamEnable)
            {
                // list of receivers found on the network; the buffer must outlive
                // the combobox dialog, which keeps a pointer to it
                static char tvOptions[MaxDiscoveredOptions * 96 + 64];
                static int tvSelection = 0;
                static int tvApplied = 0;
                static int tvLastCount = -1;
                Stream::DiscoveryTick();
                int count = std::min(Stream::DiscoveredCount(), MaxDiscoveredOptions);
                char* p = tvOptions;
                p += sprintf(p, "%s", count ? "Choose a TV..." : "Searching for TVs... (start the melonDS TV app)") + 1;
                int current = 0;
                for (int i = 0; i < count; i++)
                {
                    const Stream::Device& dev = Stream::Discovered(i);
                    if (strcmp(dev.Host, Config::StreamHost) == 0)
                        current = i + 1;
                    p += snprintf(p, 96, "%s (%s)", dev.Name, dev.Host) + 1;
                }
                *p = '\0';
                if (count != tvLastCount)
                {
                    // the list changed: re-sync the selection with the configured host
                    tvSelection = current;
                    tvApplied = current;
                    tvLastCount = count;
                }
                DoCombobox(settingsFrame, settingsSkewer, "TVs found", tvOptions, tvSelection);
                if (tvSelection != tvApplied)
                {
                    if (tvSelection >= 1 && tvSelection <= count)
                    {
                        strncpy(Config::StreamHost, Stream::Discovered(tvSelection - 1).Host, sizeof(Config::StreamHost) - 1);
                        Config::StreamHost[sizeof(Config::StreamHost) - 1] = '\0';
                    }
                    tvApplied = tvSelection;
                }

                DoTextField(settingsFrame, settingsSkewer, "TV address (manual)", Config::StreamHost, sizeof(Config::StreamHost));

                bool hideTop = Config::StreamHideTop;
                DoCheckbox(settingsFrame, settingsSkewer, "Show only the bottom screen on the Switch", hideTop);
                Config::StreamHideTop = hideTop;

                DoCombobox(settingsFrame, settingsSkewer, "Image", "JPEG (less bandwidth)\0Lossless (best quality, needs a strong wifi)\0Auto (lossless for 2D, JPEG for 3D)\0", Config::StreamCodec);
                if (Config::StreamCodec != 1)
                    DoSlider(settingsFrame, settingsSkewer, "JPEG quality (90+ keeps colors sharp)", Config::StreamQuality, 10, 100);
                DoCombobox(settingsFrame, settingsSkewer, "Audio", "Switch speakers\0TV\0", Config::StreamAudio);
                int frameSkip = Config::StreamFrameSkip - 1;
                if (frameSkip < 0) frameSkip = 0;
                if (frameSkip > 2) frameSkip = 2;
                DoCombobox(settingsFrame, settingsSkewer, "Send", "Every frame (60 fps)\0Every 2nd frame (30 fps)\0Every 3rd frame (20 fps)\0", frameSkip);
                Config::StreamFrameSkip = frameSkip + 1;
            }
        }
        Emulation::UpdateScreenLayout();
        break;
    case uiScreen_RetroAchievements:
        title = "Achievements";
        {    
            if (g_loadAchievements) {
                g_achievements = achievements_list();   
            }
            if (g_achievements.empty()) {
                DoLabel(settingsFrame, settingsSkewer, "This title does not have any achievements.");
                DoLabel(settingsFrame, settingsSkewer, "Please check the RetroAchievements website for more information.");
            } else {
                for (const auto& ach : g_achievements) {
                    SectionHeader(settingsFrame, settingsSkewer, ach.title.c_str());
                    DoLabel(settingsFrame, settingsSkewer, ach.description.c_str());
                    DoLabel(settingsFrame, settingsSkewer, ach.progress.c_str());
                    if (ach.textureId >= 0)
                        ShowImage(settingsFrame, settingsSkewer, ach.textureId, ach.width, ach.height);
                }
                
            }
        }
        break;
    case uiScreen_InputSettings:
        title = "Input";

        padUpdate(&pad);
        {
            SectionHeader(settingsFrame, settingsSkewer, "Touchscreen");
            
            DoCombobox(settingsFrame, settingsSkewer, "Cursor mode", "Mouse mode\0Offset mode\0Motion controls!\0", Config::TouchscreenMode, true);
            DoCombobox(settingsFrame, settingsSkewer, "Click mode", "Hold\0Toggle\0", Config::TouchscreenClickMode);
            bool leftHanded = Config::LeftHandedMode;
            DoCheckbox(settingsFrame, settingsSkewer, "Left handed mode", leftHanded);
            Config::LeftHandedMode = leftHanded;
        }
        {
            SectionHeader(settingsFrame, settingsSkewer, "Joycon");
            bool fastforward = Config::FastForward;
            DoCheckbox(settingsFrame, settingsSkewer, "Hold to fastforward (ZL)", fastforward);
            Config::FastForward = fastforward;
        }
        {
            static bool defaultMapping = false;

            static bool saveMapping = false;
            static bool loadMapping = false;

            SectionHeader(settingsFrame, settingsSkewer, "Buttons Remapping");

            DoInputButton(settingsFrame, settingsSkewer, "A: ", InputConfig::ButtonA);
            DoInputButton(settingsFrame, settingsSkewer, "B: ", InputConfig::ButtonB);
            DoInputButton(settingsFrame, settingsSkewer, "X: ", InputConfig::ButtonX);
            DoInputButton(settingsFrame, settingsSkewer, "Y: ", InputConfig::ButtonY);
            DoInputButton(settingsFrame, settingsSkewer, "Left Stick Button: ", InputConfig::ButtonStickL);
            DoInputButton(settingsFrame, settingsSkewer, "Right Stick Button: ", InputConfig::ButtonStickR);
            DoInputButton(settingsFrame, settingsSkewer, "L: ", InputConfig::ButtonL);
            DoInputButton(settingsFrame, settingsSkewer, "R: ", InputConfig::ButtonR);
            DoInputButton(settingsFrame, settingsSkewer, "ZL: ", InputConfig::ButtonZL);
            DoInputButton(settingsFrame, settingsSkewer, "ZR: ", InputConfig::ButtonZR);
            DoInputButton(settingsFrame, settingsSkewer, "Start: ", InputConfig::ButtonStart);
            DoInputButton(settingsFrame, settingsSkewer, "Select: ", InputConfig::ButtonSelect);
            DoInputButton(settingsFrame, settingsSkewer, "Up: ", InputConfig::ButtonUp);
            DoInputButton(settingsFrame, settingsSkewer, "Down: ", InputConfig::ButtonDown);
            DoInputButton(settingsFrame, settingsSkewer, "Left: ", InputConfig::ButtonLeft);
            DoInputButton(settingsFrame, settingsSkewer, "Right: ", InputConfig::ButtonRight);

            DoInputButton(settingsFrame, settingsSkewer, "Left Stick Up: ", InputConfig::ButtonStickLUp);
            DoInputButton(settingsFrame, settingsSkewer, "Left Stick Right: ", InputConfig::ButtonStickLRight);
            DoInputButton(settingsFrame, settingsSkewer, "Left Stick Down: ", InputConfig::ButtonStickLDown);
            DoInputButton(settingsFrame, settingsSkewer, "Left Stick Left: ", InputConfig::ButtonStickLLeft);

            DoInputButton(settingsFrame, settingsSkewer, "Right Stick Up: ", InputConfig::ButtonStickRUp);
            DoInputButton(settingsFrame, settingsSkewer, "Right Stick Right: ", InputConfig::ButtonStickRRight);
            DoInputButton(settingsFrame, settingsSkewer, "Right Stick Down: ", InputConfig::ButtonStickRDown);
            DoInputButton(settingsFrame, settingsSkewer, "Right Stick Left: ", InputConfig::ButtonStickRLeft);

            DoInputButton(settingsFrame, settingsSkewer, "Left SL: ", InputConfig::ButtonLeftSL);
            DoInputButton(settingsFrame, settingsSkewer, "Left SR ", InputConfig::ButtonLeftSR);
            DoInputButton(settingsFrame, settingsSkewer, "Right SL: ", InputConfig::ButtonRightSL);
            DoInputButton(settingsFrame, settingsSkewer, "Right SR: ", InputConfig::ButtonRightSR);

            SectionHeader(settingsFrame, settingsSkewer, "Hotkeys Remapping");

            DoInputButton(settingsFrame, settingsSkewer, "Pause ", InputConfig::Pause);
            DoInputButton(settingsFrame, settingsSkewer, "Simulate Mic Noise: ", InputConfig::MicNoise);
            DoInputButton(settingsFrame, settingsSkewer, "Change Main Screen: ", InputConfig::changeScreen);
            DoInputButton(settingsFrame, settingsSkewer, "FastForward: ", InputConfig::fastForward);
            DoInputButton(settingsFrame, settingsSkewer, "QuickSave: ", InputConfig::QuickSave);
            DoInputButton(settingsFrame, settingsSkewer, "QuickLoad: ", InputConfig::QuickLoad);
            
            DoCheckbox(settingsFrame, settingsSkewer, "Reset to default", defaultMapping);
            DoCheckbox(settingsFrame, settingsSkewer, "Save this configuration", saveMapping);
            DoCheckbox(settingsFrame, settingsSkewer, "Load old configuration", loadMapping);

            if (defaultMapping) {
                InputConfig::ResetToDefault();
                defaultMapping = false;
            }

            if (loadMapping) {
                InputConfig::loadMappingFromFile("sdmc:/switch/melonDS/input.cfg");
                loadMapping = false;
            }

            if (saveMapping) {
                InputConfig::saveMappingToFile("sdmc:/switch/melonDS/input.cfg");
                saveMapping = false;
            }    

        }
        break;
    }
    Gfx::PopScissor();

    BackButton::DoGui(parent, title);

    KeyExplanation::Explain(KeyExplanation::button_B, "Back");
    if (BoxGui::CancelPressed())
        BackButton::GoBack();
}

}
