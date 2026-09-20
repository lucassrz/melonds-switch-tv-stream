#include "StartMenu.h"

#include "Style.h"
#include "KeyExplanations.h"
#include "main.h"
#include "ROMMetaDatabase.h"
#include "ErrorDialog.h"
#include "PlatformConfig.h"
#include "RetroAchievements.h"
#include "../FrontendUtil.h"

#include "stb_image/stb_image.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <vector>
#include <algorithm>

namespace StartMenu
{

struct LastPlayedROM
{
    int TitleIconIdx;
    std::string Path;
};

std::vector<LastPlayedROM> LastPlayedROMs;

struct LibraryEntry
{
    std::string Path;
    int MetaIdx;
};

std::vector<LibraryEntry> Library;
std::string LibraryPath;

u32 MelonLogoTexture;
u32 SavestateMask;

enum
{
    nav_Library,
    nav_Browse,
    nav_BootFirmware,
    nav_Emulation,
    nav_Display,
    nav_Streaming,
    nav_Input,
    nav_Exit,
    nav_TVCard,
    // pause menu
    nav_Continue = 0,
    nav_Lid,
    nav_Reset,
    nav_Achievements,
    nav_PauseDisplay,
    nav_PauseInput,
    nav_Close,
};

static bool HasNdsExtension(const char* name)
{
    int len = strlen(name);
    if (len < 4) return false;
    const char* ext = name + len - 4;
    return strcasecmp(ext, ".nds") == 0 || strcasecmp(ext, ".dsi") == 0 || strcasecmp(ext, ".srl") == 0;
}

void RefreshLibrary(const char* folder)
{
    Library.clear();
    LibraryPath = folder;

    DIR* dir = opendir(folder);
    if (!dir)
        return;
    std::string base = folder;
    if (base.empty() || base.back() != '/')
        base += '/';

    while (dirent* entry = readdir(dir))
    {
        if (entry->d_type != DT_REG || !HasNdsExtension(entry->d_name))
            continue;
        std::string path = base + entry->d_name;
        int idx = ROMMetaDatabase::QueryMeta(entry->d_name, path.c_str());
        Library.push_back({path, idx});
    }
    closedir(dir);

    std::sort(Library.begin(), Library.end(), [](const LibraryEntry& a, const LibraryEntry& b)
    {
        return strcasecmp(ROMMetaDatabase::Database[a.MetaIdx].Title(ROMMetaDatabase::TitleLanguage),
            ROMMetaDatabase::Database[b.MetaIdx].Title(ROMMetaDatabase::TitleLanguage)) < 0;
    });
    ROMMetaDatabase::UpdateTexture();
}

void Init()
{
    MelonLogoTexture = Gfx::TextureCreate(128, 128, DkImageFormat_RGBA8_Unorm);
    int w, h, c;
    u8* melonData = stbi_load("romfs:/melon_128x128.png", &w, &h, &c, 4);
    Gfx::TextureUpload(MelonLogoTexture, 0, 0, 128, 128, melonData, 128*4);

    for (int i = 0; i < 5; i++)
    {
        FILE* f = fopen(Config::LastROMPath[i], "rb");
        if (!f) continue;
        fclose(f);

        const char* romname = strrchr(Config::LastROMPath[i], '/') + 1;
        LastPlayedROMs.push_back({ROMMetaDatabase::QueryMeta(romname, Config::LastROMPath[i]), Config::LastROMPath[i]});
    }
    ROMMetaDatabase::UpdateTexture();

    RefreshLibrary(Config::LastROMFolder);
}

void PushLastPlayed(const std::string& newPath, int titleIconIdx)
{
    SavestateMask = 0;
    for (int i = 0; i < 8; i++)
    {
        if (Frontend::SavestateExists(i + 1))
            SavestateMask |= 1 << i;
    }

    for (u32 i = 0; i < LastPlayedROMs.size(); i++)
    {
        if (LastPlayedROMs[i].Path == newPath)
        {
            // it's already in there, move it to the top
            LastPlayedROM entry = LastPlayedROMs[i];
            LastPlayedROMs.erase(LastPlayedROMs.begin() + i);
            LastPlayedROMs.insert(LastPlayedROMs.begin(), entry);
            return;
        }
    }

    if (LastPlayedROMs.size() >= 5)
        LastPlayedROMs.pop_back();
    LastPlayedROMs.insert(LastPlayedROMs.begin(), {titleIconIdx, newPath});
}

void DeInit()
{
    for (int i = 0; i < 5; i++)
    {
        if (i >= LastPlayedROMs.size())
            strcpy(Config::LastROMPath[i], "");
        else
            strcpy(Config::LastROMPath[i], LastPlayedROMs[i].Path.c_str());
    }

    Gfx::TextureDelete(MelonLogoTexture);
}

// ---- drawing helpers ---------------------------------------------------------

static void DrawRomIcon(ROMMetaDatabase::ROMMeta& meta, Gfx::Vector2f position, float size, float radius)
{
    Gfx::DrawRoundedRect(position, {size, size}, RaisedColor, radius);
    if (!meta.HasIcon)
        return;
    float pad = size * 0.12f;
    Gfx::SetSampler(Gfx::sampler_Nearest | Gfx::sampler_ClampToEdge);
    Gfx::DrawRectangle(meta.Icon.AtlasTexture,
        position + Gfx::Vector2f{pad, pad}, {size - 2.f * pad, size - 2.f * pad},
        {(float)meta.Icon.PackX, (float)meta.Icon.PackY}, {32.f, 32.f},
        {1.f, 1.f, 1.f, 1.f}, false, radius * 0.5f);
    Gfx::SetSampler(Gfx::sampler_Linear | Gfx::sampler_ClampToEdge);
}

// DS titles hold the game name and the publisher on separate lines.
static void SplitTitle(const char* title, std::string& name, std::string& publisher)
{
    const char* nl = strchr(title, '\n');
    if (!nl)
    {
        name = title;
        publisher.clear();
        return;
    }
    name.assign(title, nl - title);
    publisher = nl + 1;
    // some titles use a third line for the publisher, keep only the last one
    size_t last = publisher.rfind('\n');
    if (last != std::string::npos)
        publisher = publisher.substr(last + 1);
}

static void StartGame(const std::string& path, int metaIdx, bool onTV)
{
    if (onTV)
        Config::StreamEnable = 1;
    Emulation::LoadROM(path.c_str());
    PushLastPlayed(path, metaIdx);
}

// A sidebar entry: pill highlight when selected. Returns true when activated.
static bool NavEntry(BoxGui::Frame& sidebar, BoxGui::Skewer& skewer, const char* name, int idx, bool first = false)
{
    BoxGui::Frame frame{sidebar, skewer.Spit({sidebar.Area.Size.X, 48.f + 6.f}, Gfx::align_Right), {0.f, 3.f}, {0.f, 3.f}};
    bool selected = BoxGui::InputElement(frame, BoxGui::MakeUniqueName("sidebar", idx), first);
    if (selected)
    {
        KeyExplanation::Explain(KeyExplanation::button_A, "Select");
        Gfx::DrawRoundedRect(frame.Area.Position, frame.Area.Size, AccentColor, UIRadius);
    }
    Gfx::DrawText(Gfx::SystemFontStandard, frame.Area.Position + Gfx::Vector2f{16.f, frame.Area.Size.Y / 2.f},
        TextLineHeight, selected ? BgColor : TextSoftColor, Gfx::align_Left, Gfx::align_Center, name);
    return selected && BoxGui::ConfirmPressed();
}

// A filled or outlined button. Returns true when activated.
static bool Button(BoxGui::Frame& parent, BoxGui::Rect rect, const char* label, u64 name, bool primary, const char* hint)
{
    BoxGui::Frame frame{parent, rect};
    bool selected = BoxGui::InputElement(frame, name);
    if (primary)
    {
        Gfx::DrawRoundedRect(frame.Area.Position, frame.Area.Size, AccentColor, 14.f);
        if (selected)
            Gfx::DrawRoundedOutline(frame.Area.Position - Gfx::Vector2f{4.f, 4.f}, frame.Area.Size + Gfx::Vector2f{8.f, 8.f}, TextColor, 18.f, 2.f);
    }
    else
    {
        if (selected)
            Gfx::DrawRoundedRect(frame.Area.Position, frame.Area.Size, RaisedColor, 14.f);
        Gfx::DrawRoundedOutline(frame.Area.Position, frame.Area.Size, selected ? AccentColor : LineColor, 14.f, selected ? 2.f : 1.f);
    }
    Gfx::DrawText(Gfx::SystemFontStandard, frame.Area.Position + frame.Area.Size * 0.5f, TextLineHeight,
        primary ? BgColor : TextSoftColor, Gfx::align_Center, Gfx::align_Center, label);
    if (selected)
        KeyExplanation::Explain(KeyExplanation::button_A, hint);
    return selected && BoxGui::ConfirmPressed();
}

static void DrawSidebarHeader(BoxGui::Frame& sidebar, BoxGui::Skewer& skewer)
{
    BoxGui::Frame logoRow{sidebar, skewer.Spit({sidebar.Area.Size.X, 44.f}, Gfx::align_Right)};
    Gfx::Vector2f pos = logoRow.Area.Position + Gfx::Vector2f{8.f, 0.f};
    Gfx::DrawRectangle(MelonLogoTexture, pos, {44.f, 44.f}, {}, {128.f, 128.f}, {1.f, 1.f, 1.f, 1.f}, false, UIRadius);
    Gfx::DrawText(Gfx::SystemFontStandard, pos + Gfx::Vector2f{58.f, 12.f}, TextLineHeight * 1.2f, TextColor,
        Gfx::align_Left, Gfx::align_Center, "melonDS");
    Gfx::DrawText(Gfx::SystemFontStandard, pos + Gfx::Vector2f{58.f, 33.f}, TextLineHeight * 0.7f, TextMutedColor,
        Gfx::align_Left, Gfx::align_Center, "Switch · TV Stream");
}

// TV status card at the bottom of the sidebar; activating it opens the streaming settings.
static void TVCard(BoxGui::Frame& sidebar, BoxGui::Skewer& skewer)
{
    BoxGui::Frame frame{sidebar, skewer.Spit({sidebar.Area.Size.X, 56.f}, Gfx::align_Right)};
    bool selected = BoxGui::InputElement(frame, BoxGui::MakeUniqueName("sidebar", nav_TVCard));
    bool linked = Config::StreamEnable && Config::StreamHost[0] != '\0';

    Gfx::DrawRoundedRect(frame.Area.Position, frame.Area.Size, RaisedColor, UIRadius);
    if (selected)
        Gfx::DrawRoundedOutline(frame.Area.Position, frame.Area.Size, AccentColor, UIRadius, 2.f);
    Gfx::DrawCircle(frame.Area.Position + Gfx::Vector2f{18.f, frame.Area.Size.Y / 2.f}, 4.f, linked ? AccentColor : TextMutedColor);
    Gfx::DrawText(Gfx::SystemFontStandard, frame.Area.Position + Gfx::Vector2f{32.f, 19.f}, TextLineHeight * 0.85f, TextColor,
        Gfx::align_Left, Gfx::align_Center, linked ? (Config::StreamHostName[0] ? Config::StreamHostName : Config::StreamHost) : "No TV linked");
    Gfx::DrawText(Gfx::SystemFontStandard, frame.Area.Position + Gfx::Vector2f{32.f, 38.f}, TextLineHeight * 0.7f, TextMutedColor,
        Gfx::align_Left, Gfx::align_Center, linked ? "Top screen goes to the TV" : "Set up TV streaming");

    if (selected)
    {
        KeyExplanation::Explain(KeyExplanation::button_A, "TV settings");
        if (BoxGui::ConfirmPressed())
        {
            FocusStreamingSection = true;
            CurrentUiScreen = uiScreen_DisplaySettings;
        }
    }
}

// ---- screens -----------------------------------------------------------------

static void DoHome(BoxGui::Frame& mainFrame)
{
    if (LibraryPath != Config::LastROMFolder)
        RefreshLibrary(Config::LastROMFolder);

    BoxGui::Skewer vskewer{mainFrame, 0.f, BoxGui::direction_Vertical};
    const float pad = 24.f;

    // ---- continue playing
    if (!LastPlayedROMs.empty())
    {
        const float heroHeight = 168.f;
        BoxGui::Frame hero{mainFrame, vskewer.Spit({mainFrame.Area.Size.X, heroHeight}, Gfx::align_Right)};
        Gfx::DrawRoundedRect(hero.Area.Position, hero.Area.Size, CardColor, UIRadiusLarge);
        Gfx::DrawRoundedOutline(hero.Area.Position, hero.Area.Size, BorderColor, UIRadiusLarge, 1.f);

        LastPlayedROM& last = LastPlayedROMs[0];
        ROMMetaDatabase::ROMMeta& meta = ROMMetaDatabase::Database[last.TitleIconIdx];
        float iconSize = heroHeight - 2.f * pad;
        DrawRomIcon(meta, hero.Area.Position + Gfx::Vector2f{pad, pad}, iconSize, 16.f);

        float textX = pad + iconSize + 24.f;
        Gfx::DrawText(Gfx::SystemFontStandard, hero.Area.Position + Gfx::Vector2f{textX, pad + 4.f}, TextLineHeight * 0.7f, AccentColor,
            Gfx::align_Left, Gfx::align_Center, "CONTINUE PLAYING");
        std::string name, publisher;
        SplitTitle(meta.Title(ROMMetaDatabase::TitleLanguage), name, publisher);
        const char* file = strrchr(last.Path.c_str(), '/');
        Gfx::DrawText(Gfx::SystemFontStandard, hero.Area.Position + Gfx::Vector2f{textX, pad + 46.f}, TextLineHeight * 1.6f, TextColor,
            Gfx::align_Left, Gfx::align_Center, name.c_str());
        Gfx::DrawText(Gfx::SystemFontStandard, hero.Area.Position + Gfx::Vector2f{textX, pad + 80.f}, TextLineHeight * 0.9f, TextSoftColor,
            Gfx::align_Left, Gfx::align_Center, publisher.c_str());
        Gfx::DrawText(Gfx::SystemFontStandard, hero.Area.Position + Gfx::Vector2f{textX, pad + 106.f}, TextLineHeight * 0.75f, TextMutedColor,
            Gfx::align_Left, Gfx::align_Center, file ? file + 1 : last.Path.c_str());

        const float buttonWidth = 170.f;
        float bx = hero.Area.Size.X - pad - buttonWidth;
        if (Button(hero, {{bx, pad}, {buttonWidth, 52.f}}, "Resume", BoxGui::MakeUniqueName("hero", 0), true, "Play"))
            StartGame(last.Path, last.TitleIconIdx, false);
        if (Button(hero, {{bx, pad + 52.f + 12.f}, {buttonWidth, 44.f}}, "Play on TV", BoxGui::MakeUniqueName("hero", 1), false, "Play with TV stream"))
            StartGame(last.Path, last.TitleIconIdx, true);

        vskewer.Advance(24.f);
    }

    // ---- library header
    {
        BoxGui::Frame header{mainFrame, vskewer.Spit({mainFrame.Area.Size.X, 32.f}, Gfx::align_Right)};
        Gfx::DrawText(Gfx::SystemFontStandard, header.Area.Position + Gfx::Vector2f{0.f, 16.f}, TextLineHeight * 1.2f, TextColor,
            Gfx::align_Left, Gfx::align_Center, "Library");
        char summary[600];
        snprintf(summary, sizeof(summary), "%d game%s · %s", (int)Library.size(), Library.size() == 1 ? "" : "s", LibraryPath.c_str());
        Gfx::DrawText(Gfx::SystemFontStandard, header.Area.Position + Gfx::Vector2f{header.Area.Size.X, 18.f}, TextLineHeight * 0.75f, TextMutedColor,
            Gfx::align_Right, Gfx::align_Center, summary);
        vskewer.Advance(12.f);
    }

    // ---- library grid (leave room for the key hints at the bottom)
    float gridHeight = vskewer.RemainingLength() - 64.f;
    BoxGui::Frame grid{mainFrame, vskewer.Spit({mainFrame.Area.Size.X, gridHeight}, Gfx::align_Right),
        {0.f, 0.f}, {0.f, 0.f},
        BoxGui::direction_Vertical, BoxGui::MakeUniqueName("library", -1), false, true};
    Gfx::PushScissor(grid.Area.Position.X, grid.Area.Position.Y, grid.Area.Size.X, grid.Area.Size.Y);

    if (Library.empty())
    {
        BoxGui::Frame empty{grid, {{0.f, 0.f}, {grid.Area.Size.X, 120.f}}};
        Gfx::DrawRoundedRect(empty.Area.Position, empty.Area.Size, CardColor, UIRadiusLarge);
        Gfx::DrawRoundedOutline(empty.Area.Position, empty.Area.Size, BorderColor, UIRadiusLarge, 1.f);
        Gfx::DrawText(Gfx::SystemFontStandard, empty.Area.Position + Gfx::Vector2f{pad, 40.f}, TextLineHeight * 1.1f, TextColor,
            Gfx::align_Left, Gfx::align_Center, "No DS games in this folder");
        Gfx::DrawText(Gfx::SystemFontStandard, empty.Area.Position + Gfx::Vector2f{pad, 76.f}, TextLineHeight * 0.85f, TextMutedColor,
            Gfx::align_Left, Gfx::align_Center, "Use \"Browse files\" to open the folder that holds your .nds dumps.");
    }
    else
    {
        const int columns = 4;
        const float gap = 20.f;
        float tileWidth = (grid.Area.Size.X - gap * (columns - 1)) / columns;
        float iconSize = tileWidth - 28.f;
        float tileHeight = 14.f + iconSize + 12.f + TextLineHeight * 2.1f + 14.f;

        for (u32 i = 0; i < Library.size(); i++)
        {
            int col = i % columns, row = i / columns;
            BoxGui::Frame tile{grid, {{col * (tileWidth + gap), row * (tileHeight + gap)}, {tileWidth, tileHeight}}};
            bool selected = BoxGui::InputElement(tile, BoxGui::MakeUniqueName("library_tile", i), LastPlayedROMs.empty() && i == 0);
            if (!tile.IsVisible())
                continue;

            ROMMetaDatabase::ROMMeta& meta = ROMMetaDatabase::Database[Library[i].MetaIdx];
            Gfx::DrawRoundedRect(tile.Area.Position, tile.Area.Size, CardColor, 16.f);
            if (selected)
                Gfx::DrawRoundedOutline(tile.Area.Position, tile.Area.Size, AccentColor, 16.f, 2.f);
            DrawRomIcon(meta, tile.Area.Position + Gfx::Vector2f{14.f, 14.f}, iconSize, 12.f);

            std::string name, publisher;
            SplitTitle(meta.Title(ROMMetaDatabase::TitleLanguage), name, publisher);
            Gfx::PushScissor(tile.Area.Position.X, tile.Area.Position.Y, tile.Area.Size.X, tile.Area.Size.Y);
            Gfx::DrawText(Gfx::SystemFontStandard, tile.Area.Position + Gfx::Vector2f{14.f, 14.f + iconSize + 12.f + TextLineHeight * 0.45f},
                TextLineHeight * 0.85f, TextColor, Gfx::align_Left, Gfx::align_Center, name.c_str());
            Gfx::DrawText(Gfx::SystemFontStandard, tile.Area.Position + Gfx::Vector2f{14.f, 14.f + iconSize + 12.f + TextLineHeight * 1.5f},
                TextLineHeight * 0.7f, TextMutedColor, Gfx::align_Left, Gfx::align_Center, publisher.c_str());
            Gfx::PopScissor();

            if (selected)
            {
                KeyExplanation::Explain(KeyExplanation::button_A, "Play");
                KeyExplanation::Explain(KeyExplanation::button_X, "Play on TV");
                if (BoxGui::ConfirmPressed())
                    StartGame(Library[i].Path, Library[i].MetaIdx, false);
                else if (BoxGui::AltPressed())
                    StartGame(Library[i].Path, Library[i].MetaIdx, true);
            }
        }
    }
    Gfx::PopScissor();
}

static void DoSavestateRow(BoxGui::Frame& mainFrame, BoxGui::Skewer& vskewer, const char* title, bool loading)
{
    BoxGui::Frame titleFrame{mainFrame, vskewer.Spit({mainFrame.Area.Size.X, 28.f}, Gfx::align_Right)};
    Gfx::DrawText(Gfx::SystemFontStandard, titleFrame.Area.Position + Gfx::Vector2f{0.f, 14.f}, TextLineHeight * 0.8f, TextMutedColor,
        Gfx::align_Left, Gfx::align_Center, title);

    const float buttonSize = 56.f, gap = 10.f;
    int count = loading ? 9 : 8;
    BoxGui::Frame rowFrame{mainFrame, vskewer.Spit({mainFrame.Area.Size.X, buttonSize}, Gfx::align_Right)};
    int selectedSlot = -1;
    for (int i = 0; i < count; i++)
    {
        BoxGui::Frame button{rowFrame, {{i * (buttonSize + gap), 0.f}, {i == 8 ? buttonSize * 1.6f : buttonSize, buttonSize}}};
        bool enabled = Config::ConsoleType != 1
            && (!loading || (i < 8 ? (SavestateMask & (1 << i)) : Frontend::SavestateLoaded));
        bool selected = false;
        if (enabled)
        {
            selected = BoxGui::InputElement(button, BoxGui::MakeUniqueName(loading ? "loadstate" : "savestate", i));
            if (selected)
                selectedSlot = i;
        }
        Gfx::DrawRoundedRect(button.Area.Position, button.Area.Size, selected ? AccentColor : RaisedColor, UIRadius);
        if (!enabled)
            Gfx::DrawRoundedOutline(button.Area.Position, button.Area.Size, BorderColor, UIRadius, 1.f);
        char label[8];
        if (i == 8)
            strcpy(label, "Undo");
        else
            snprintf(label, sizeof(label), "%d", i + 1);
        Gfx::DrawText(Gfx::SystemFontStandard, button.Area.Position + button.Area.Size * 0.5f, TextLineHeight,
            selected ? BgColor : (enabled ? TextColor : TextMutedColor), Gfx::align_Center, Gfx::align_Center, label);
    }
    vskewer.Advance(20.f);

    if (selectedSlot == -1)
        return;
    KeyExplanation::Explain(KeyExplanation::button_A, loading ? "Load state" : "Save state");
    if (!BoxGui::ConfirmPressed())
        return;

    if (!loading)
    {
        char filename[512];
        Frontend::GetSavestateName(selectedSlot + 1, filename, 512);
        if (Frontend::SaveState(filename))
            SavestateMask |= 1 << selectedSlot;
        else
            ErrorDialog::Open("Failed to create savestate");
    }
    else
    {
        bool loadedSuccessfully;
        if (selectedSlot < 8)
        {
            char filename[512];
            Frontend::GetSavestateName(selectedSlot + 1, filename, 512);
            loadedSuccessfully = Frontend::LoadState(filename);
        }
        else
        {
            Frontend::UndoStateLoad();
            loadedSuccessfully = true;
        }
        if (loadedSuccessfully)
        {
            Emulation::SetPause(false);
            BoxGui::ForceSelecton(BoxGui::MakeUniqueName("sidebar", nav_Continue));
        }
        else
        {
            ErrorDialog::Open("Couldn't load savefile");
        }
    }
}

static void DoPause(BoxGui::Frame& mainFrame)
{
    BoxGui::Skewer vskewer{mainFrame, 0.f, BoxGui::direction_Vertical};
    const float pad = 24.f;

    if (!LastPlayedROMs.empty())
    {
        const float heroHeight = 120.f;
        BoxGui::Frame hero{mainFrame, vskewer.Spit({mainFrame.Area.Size.X, heroHeight}, Gfx::align_Right)};
        Gfx::DrawRoundedRect(hero.Area.Position, hero.Area.Size, CardColor, UIRadiusLarge);
        Gfx::DrawRoundedOutline(hero.Area.Position, hero.Area.Size, BorderColor, UIRadiusLarge, 1.f);
        LastPlayedROM& last = LastPlayedROMs[0];
        ROMMetaDatabase::ROMMeta& meta = ROMMetaDatabase::Database[last.TitleIconIdx];
        float iconSize = heroHeight - 2.f * pad;
        DrawRomIcon(meta, hero.Area.Position + Gfx::Vector2f{pad, pad}, iconSize, 12.f);
        float textX = pad + iconSize + 20.f;
        Gfx::DrawText(Gfx::SystemFontStandard, hero.Area.Position + Gfx::Vector2f{textX, pad + 4.f}, TextLineHeight * 0.7f, AccentColor,
            Gfx::align_Left, Gfx::align_Center, "PAUSED");
        std::string name, publisher;
        SplitTitle(meta.Title(ROMMetaDatabase::TitleLanguage), name, publisher);
        Gfx::DrawText(Gfx::SystemFontStandard, hero.Area.Position + Gfx::Vector2f{textX, pad + 40.f}, TextLineHeight * 1.4f, TextColor,
            Gfx::align_Left, Gfx::align_Center, name.c_str());
        vskewer.Advance(28.f);
    }

    if (Config::hardcoreMode)
        return;

    BoxGui::Frame title{mainFrame, vskewer.Spit({mainFrame.Area.Size.X, 32.f}, Gfx::align_Right)};
    Gfx::DrawText(Gfx::SystemFontStandard, title.Area.Position + Gfx::Vector2f{0.f, 16.f}, TextLineHeight * 1.2f, TextColor,
        Gfx::align_Left, Gfx::align_Center, "Save states");
    vskewer.Advance(12.f);
    DoSavestateRow(mainFrame, vskewer, "SAVE TO SLOT", false);
    DoSavestateRow(mainFrame, vskewer, "LOAD FROM SLOT", true);
}

void DoGui(BoxGui::Frame& parent)
{
    bool paused = Emulation::State == Emulation::emuState_Paused;

    // ---- sidebar
    {
        BoxGui::Frame sidebar{parent, {{0.f, 0.f}, {SidebarWidth, parent.Area.Size.Y}}, {24.f, 36.f}, {24.f, 28.f}};
        Gfx::DrawRectangle({0.f, 0.f}, {SidebarWidth, parent.Area.Size.Y}, PanelColor);
        Gfx::DrawRectangle({SidebarWidth - 1.f, 0.f}, {1.f, parent.Area.Size.Y}, BorderColor);

        BoxGui::Skewer skewer{sidebar, 0.f, BoxGui::direction_Vertical};
        DrawSidebarHeader(sidebar, skewer);
        skewer.Advance(32.f);

        if (paused)
        {
            if (NavEntry(sidebar, skewer, "Continue", nav_Continue, true))
                Emulation::SetPause(false);
            if (NavEntry(sidebar, skewer, Emulation::LidClosed ? "Open lid" : "Close lid", nav_Lid))
            {
                Emulation::LidClosed ^= true;
                Emulation::SetPause(false);
            }
            if (NavEntry(sidebar, skewer, "Reset", nav_Reset))
                Emulation::Reset();
            skewer.Advance(12.f);
            if (isConnected() && NavEntry(sidebar, skewer, "Achievements", nav_Achievements))
                CurrentUiScreen = uiScreen_RetroAchievements;
            if (NavEntry(sidebar, skewer, "Display", nav_PauseDisplay))
                CurrentUiScreen = uiScreen_DisplaySettings;
            if (NavEntry(sidebar, skewer, "Input", nav_PauseInput))
                CurrentUiScreen = uiScreen_InputSettings;

            KeyExplanation::Explain(KeyExplanation::button_B, "Unpause");
            if (BoxGui::CancelPressed())
                Emulation::SetPause(false);
        }
        else
        {
            if (NavEntry(sidebar, skewer, "Library", nav_Library, true))
                BoxGui::ForceSelecton(BoxGui::MakeUniqueName(LastPlayedROMs.empty() ? "library_tile" : "hero", 0), false);
            if (NavEntry(sidebar, skewer, "Browse files", nav_Browse))
                CurrentUiScreen = uiScreen_BrowseROM;
            if (NavEntry(sidebar, skewer, "Boot firmware", nav_BootFirmware))
                Emulation::LoadBIOS();
            skewer.Advance(12.f);
            if (NavEntry(sidebar, skewer, "Emulation", nav_Emulation))
                CurrentUiScreen = uiScreen_EmulationSettings;
            if (NavEntry(sidebar, skewer, "Display", nav_Display))
                CurrentUiScreen = uiScreen_DisplaySettings;
            if (NavEntry(sidebar, skewer, "TV streaming", nav_Streaming))
            {
                FocusStreamingSection = true;
                CurrentUiScreen = uiScreen_DisplaySettings;
            }
            if (NavEntry(sidebar, skewer, "Input", nav_Input))
                CurrentUiScreen = uiScreen_InputSettings;
        }

        // bottom of the sidebar, laid out upwards
        BoxGui::Skewer bottom{sidebar, 0.f, BoxGui::direction_Vertical};
        bottom.AlignRight(0.f);
        if (paused)
        {
            if (NavEntry(sidebar, bottom, "Close game", nav_Close))
            {
                Emulation::Stop();
                g_loadAchievements = true;
            }
        }
        else
        {
            if (NavEntry(sidebar, bottom, "Exit", nav_Exit))
                Done = true;
        }
        bottom.Advance(12.f);
        TVCard(sidebar, bottom);
    }

    // ---- main area
    {
        BoxGui::Frame mainFrame{parent, {{SidebarWidth, 0.f}, {parent.Area.Size.X - SidebarWidth, parent.Area.Size.Y}},
            {UIPagePadding, UIPagePadding}, {UIPagePadding, 0.f}};
        if (paused)
            DoPause(mainFrame);
        else
            DoHome(mainFrame);
    }
}

}
