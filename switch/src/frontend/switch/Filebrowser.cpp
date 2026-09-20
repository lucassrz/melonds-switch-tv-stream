// for strcasestr
#define _GNU_SOURCE
#include <string.h>

#include "Filebrowser.h"
#include "StartMenu.h"

#include "Style.h"
#include "ROMMetaDatabase.h"
#include "BackButton.h"
#include "KeyExplanations.h"

#include "PlatformConfig.h"
#include "main.h"

#include <vector>
#include <string>
#include <dirent.h>
#include <assert.h>

namespace Filebrowser
{

struct Entry
{
    int Name, ROMDBEntry;
    bool IsDirectory;
};
std::vector<char> CurrentEntryNames;
std::vector<Entry> CurrentEntries;
std::string CurrentPath;
std::string SearchText;
int CurrentSelection = -1;
bool CurrentFileListingMode = true;  // true = internal metadata; false = original filenames

SwkbdConfig SearchKeyboard;

void Init()
{
    EnterDirectory(Config::LastROMFolder);

    swkbdCreate(&SearchKeyboard, 0);
    swkbdConfigMakePresetDefault(&SearchKeyboard);
    swkbdConfigSetOkButtonText(&SearchKeyboard, "Go");
    swkbdConfigSetHeaderText(&SearchKeyboard, "Enter text to search for");
}

void DeInit()
{
    swkbdClose(&SearchKeyboard);

    strcpy(Config::LastROMFolder, CurrentPath.c_str());
}

int PushString(std::vector<char>& container, const char* str)
{
    u32 stringLength = strlen(str);
    u32 offset = container.size();
    container.resize(offset + stringLength + 1);
    memcpy(&container[offset], str, stringLength + 1);
    return offset;
}

const char* FileBrowserPrefix = "filebrowser_entries";

void EnterDirectory(const char* path)
{
    BoxGui::ForceSelecton(BoxGui::MakeUniqueName(FileBrowserPrefix, 0));
    CurrentSelection = 0;

    DIR* dir = opendir(path);
    if (dir == nullptr)
    {
        path = "/";
        dir = opendir(path);
    }

    CurrentPath = path;
    strncpy(Config::LastROMFolder, CurrentPath.c_str(), sizeof(Config::LastROMFolder) - 1);
    Config::LastROMFolder[sizeof(Config::LastROMFolder) - 1] = '\0';

    CurrentEntryNames.clear();
    CurrentEntries.clear();
    if (CurrentPath != "/")
        CurrentEntries.push_back({PushString(CurrentEntryNames, ".."), -1, true});

    std::string fullpath;
    struct dirent* cur;
    while (cur = readdir(dir))
    {
        int nameLen = strlen(cur->d_name);
        if (nameLen == 1 && cur->d_name[0] == '.')
            continue;

        Entry entry;
        entry.ROMDBEntry = -1; 
        if (cur->d_type == DT_REG)
        {
            if (nameLen < 4)
                continue;

            const char* extension = cur->d_name + nameLen - 4;
            int notNdsFile = strcmp(extension, ".nds");
            int notBinFile = strcmp(extension, ".bin");
            if (notNdsFile && notBinFile)
                continue;
            // we'll get to you later
            /*int notGbaFile = strcmp(extension, ".gba");
            if (notGbaFile)
                continue;*/

            if (!notNdsFile)
            {
                fullpath.clear();
                fullpath = CurrentPath;
                if (CurrentPath != "/")
                    fullpath += '/';
                fullpath += cur->d_name;
                entry.ROMDBEntry = ROMMetaDatabase::QueryMeta(cur->d_name, fullpath.c_str());
            }
        }
        else if (cur->d_type != DT_DIR)
        {
            continue;
        }

        entry.Name = PushString(CurrentEntryNames, cur->d_name);
        entry.IsDirectory = cur->d_type == DT_DIR;
        CurrentEntries.push_back(entry);
    }

    closedir(dir);

    std::sort(CurrentEntries.begin(), CurrentEntries.end(), [](const Entry& a, const Entry& b)
    {
        if (a.IsDirectory == b.IsDirectory)
            return strcasecmp(&CurrentEntryNames[a.Name], &CurrentEntryNames[b.Name]) < 0;
        else
            return a.IsDirectory;
    });

    ROMMetaDatabase::UpdateTexture();
}

void GoUpwards()
{
    std::string newPath = CurrentPath;
    if (newPath != "/")
    {
        newPath.resize(newPath.rfind('/'));
        if (newPath.size() == 0)
            newPath = "/";
        EnterDirectory(newPath.c_str());
    }
}

void EnterDirectory(u32 entry)
{
    std::string newPath = CurrentPath;
    if (newPath != "/")
        newPath += '/';
    newPath += &CurrentEntryNames[CurrentEntries[entry].Name];
    EnterDirectory(newPath.c_str());
}

int FindNextOccurence(const char* searchText)
{
    if (CurrentEntries.size() == 0)
        return -1;
    if (CurrentSelection == -1)
        CurrentSelection = 0;
    int i = CurrentSelection + 1;
    if (i == CurrentEntries.size())
        i = 0;
    while (i != CurrentSelection)
    {
        // if there's a proper title first search it
        if (CurrentEntries[i].ROMDBEntry != -1
            && strcasestr(ROMMetaDatabase::Database[CurrentEntries[i].ROMDBEntry].Title(ROMMetaDatabase::TitleLanguage),
                searchText))
            return i;

        // always search by filename as well
        // this saves us a bunch of work, otherwise people would complain if they can't
        // find Pokémon when searching without the accent haha
        if (strcasestr(&CurrentEntryNames[CurrentEntries[i].Name], searchText))
            return i;

        i++;
        if (i == CurrentEntries.size())
            i = 0;
    }

    return -1;
}

std::string CurrentSelectionPath()
{
    std::string result(CurrentPath);
    if (CurrentPath != "/")
        result += '/';
    result += &CurrentEntryNames[CurrentEntries[CurrentSelection].Name];
    return result;
}

SwkbdTextCheckResult CheckSearch(char* tmpString, size_t tmpSize)
{
    if (FindNextOccurence(tmpString) == -1)
    {
        strcpy(tmpString, "Couldn't find any matching file or directory :(");
        return SwkbdTextCheckResult_Prompt;
    }
    return SwkbdTextCheckResult_OK;
}

void DoGui(BoxGui::Frame& parent)
{
    BackButton::DoGui(parent, CurrentPath.c_str());

    BoxGui::Frame entriesFrame{parent, {{0.f, BackButtonHeight}, {parent.Area.Size.X, parent.Area.Size.Y - BackButtonHeight}},
        {UIPagePadding, 8.f}, {UIPagePadding, 0.f},
        1, BoxGui::MakeUniqueName(FileBrowserPrefix, -1),
        false, true};
    BoxGui::Skewer entrySkewer{entriesFrame, 0.f, BoxGui::direction_Vertical};

    Gfx::PushScissor(entriesFrame.Area.Position.X, entriesFrame.Area.Position.Y, entriesFrame.Area.Size.X, entriesFrame.Area.Size.Y);

    CurrentSelection = -1;
    for (int i = 0; i < CurrentEntries.size(); i++)
    {
        Entry& entry = CurrentEntries[i];

        BoxGui::Frame entryFrame{entriesFrame, entrySkewer.Spit({entriesFrame.Area.Size.X, UIRowHeight + UIRowGap}, Gfx::align_Right),
            {0.f, UIRowGap / 2.f}, {0.f, UIRowGap / 2.f}};
        if (BoxGui::InputElement(entryFrame, BoxGui::MakeUniqueName(FileBrowserPrefix, i)))
            CurrentSelection = i;
        if (!entryFrame.IsVisible())
            continue;

        bool selected = CurrentSelection == i;
        if (selected)
        {
            Gfx::DrawRoundedRect(entryFrame.Area.Position, entryFrame.Area.Size, RaisedColor, UIRadius);
            Gfx::DrawRoundedOutline(entryFrame.Area.Position, entryFrame.Area.Size, AccentColor, UIRadius, 2.f);
        }

        const float iconSize = 40.f;
        Gfx::Vector2f iconPos = entryFrame.Area.Position + Gfx::Vector2f{12.f, (entryFrame.Area.Size.Y - iconSize) / 2.f};
        Gfx::Vector2f textPos = entryFrame.Area.Position + Gfx::Vector2f{12.f + iconSize + 16.f, entryFrame.Area.Size.Y / 2.f};

        if (entry.IsDirectory)
        {
            // a small folder glyph
            Gfx::DrawRoundedRect(iconPos + Gfx::Vector2f{6.f, 8.f}, {14.f, 8.f}, TextMutedColor, 3.f);
            Gfx::DrawRoundedRect(iconPos + Gfx::Vector2f{6.f, 13.f}, {28.f, 18.f}, TextMutedColor, 4.f);
            Gfx::DrawText(Gfx::SystemFontStandard, textPos, TextLineHeight, TextColor,
                Gfx::align_Left, Gfx::align_Center, &CurrentEntryNames[entry.Name]);
            Gfx::DrawText(Gfx::SystemFontStandard, entryFrame.Area.Position + Gfx::Vector2f{entryFrame.Area.Size.X - 16.f, entryFrame.Area.Size.Y / 2.f},
                TextLineHeight * 0.8f, TextMutedColor, Gfx::align_Right, Gfx::align_Center, i == 0 && CurrentPath != "/" ? "Up" : "Folder");
        }
        else if (entry.ROMDBEntry != -1 && CurrentFileListingMode)
        {
            ROMMetaDatabase::ROMMeta& meta = ROMMetaDatabase::Database[entry.ROMDBEntry];
            Gfx::DrawRoundedRect(iconPos, {iconSize, iconSize}, CardColor, 8.f);
            if (meta.HasIcon)
            {
                Gfx::SetSampler(Gfx::sampler_Nearest | Gfx::sampler_ClampToEdge);
                Gfx::DrawRectangle(meta.Icon.AtlasTexture,
                    iconPos + Gfx::Vector2f{4.f, 4.f}, {iconSize - 8.f, iconSize - 8.f},
                    {(float)meta.Icon.PackX, (float)meta.Icon.PackY}, {32.f, 32.f},
                    {1.f, 1.f, 1.f, 1.f}, false, 4.f);
                Gfx::SetSampler(Gfx::sampler_Linear | Gfx::sampler_ClampToEdge);
            }
            Gfx::DrawText(Gfx::SystemFontStandard, textPos, TextLineHeight, TextColor,
                Gfx::align_Left, Gfx::align_Center, meta.Title(ROMMetaDatabase::TitleLanguage));
        }
        else
        {
            Gfx::DrawRoundedRect(iconPos, {iconSize, iconSize}, CardColor, 8.f);
            Gfx::DrawText(Gfx::SystemFontStandard, textPos, TextLineHeight, TextColor,
                Gfx::align_Left, Gfx::align_Center, &CurrentEntryNames[entry.Name]);
        }
    }

    Gfx::PopScissor();

    KeyExplanation::Explain(KeyExplanation::button_Y, "Search");
    if (BoxGui::SearchPressed())
    {
        swkbdConfigSetTextCheckCallback(&SearchKeyboard, CheckSearch);
        swkbdConfigSetInitialText(&SearchKeyboard, SearchText.c_str());

        char searchText[256] = "";
        Result r = swkbdShow(&SearchKeyboard, searchText, 256);
        SearchText = searchText;
        Gfx::SkipTimestep();

        if (R_SUCCEEDED(r))
        {
            int newSelection = FindNextOccurence(SearchText.c_str());
            if (newSelection != -1)
            {
                CurrentSelection = newSelection;
                BoxGui::ForceSelecton(BoxGui::MakeUniqueName(FileBrowserPrefix, newSelection), false);
            }
        }
    }
    
    
    KeyExplanation::Explain(KeyExplanation::button_Plus, "Show filenames");
    if (BoxGui::DetailsPressed()) {
        CurrentFileListingMode = !CurrentFileListingMode;  // toggle
    }

    if (CurrentSelection != -1)
    {    
        if (BoxGui::LeftPressed())
        {
            BoxGui::ForceSelecton(BoxGui::MakeUniqueName(FileBrowserPrefix,
                std::max(CurrentSelection - 5, 0)), true);
        }
        if (BoxGui::RightPressed())
        {
            BoxGui::ForceSelecton(BoxGui::MakeUniqueName(FileBrowserPrefix,
                std::min(CurrentSelection + 5, (int)CurrentEntries.size() - 1)), true);
        }

        if (CurrentEntries[CurrentSelection].IsDirectory)
        {
            KeyExplanation::Explain(KeyExplanation::button_A, "Enter");

            if (BoxGui::ConfirmPressed())
            {
                if (CurrentSelection == 0)
                    GoUpwards();
                else
                    EnterDirectory(CurrentSelection);
            }
        }
        else
        {
            KeyExplanation::Explain(KeyExplanation::button_A, "Start");   

            if (BoxGui::ConfirmPressed())
            {
                std::string romPath = CurrentSelectionPath();
                Emulation::LoadROM(romPath.c_str());
                StartMenu::PushLastPlayed(romPath, CurrentEntries[CurrentSelection].ROMDBEntry);
                BoxGui::ForceSelecton(BoxGui::MakeUniqueName(FileBrowserPrefix, 0), false);
            }
        }
    
        if (CurrentPath != "/")
        {
            KeyExplanation::Explain(KeyExplanation::button_B, "Upwards");
    
            if (BoxGui::CancelPressed())
            {
                BoxGui::ForceSelecton(BoxGui::MakeUniqueName(FileBrowserPrefix, 0), false);
            }
        }
    }
}

}
