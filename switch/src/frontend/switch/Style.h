#ifndef STYLE_H
#define STYLE_H

#include "Gfx.h"

// Metrics
const float TextLineHeight = 20.f;
const float UIRowHeight = 56.f;        // one settings / list row
const float UIRowGap = 6.f;            // vertical gap between rows
const float BackButtonHeight = 72.f;   // header bar holding the back button and the page title
const float UIRadius = 12.f;
const float UIRadiusLarge = 20.f;
const float UIPagePadding = 40.f;
const float SidebarWidth = 280.f;

// Palette: near-black cool ground, one green accent, melon red only for the logo.
const Gfx::Color BgColor        = {(u32)0x0F1114FF};
const Gfx::Color PanelColor     = {(u32)0x14181DFF};
const Gfx::Color CardColor      = {(u32)0x171B21FF};
const Gfx::Color RaisedColor    = {(u32)0x1B2026FF};
const Gfx::Color BorderColor    = {(u32)0x22282FFF};
const Gfx::Color LineColor      = {(u32)0x2A313AFF};
const Gfx::Color TextColor      = {(u32)0xF2F4F1FF};
const Gfx::Color TextSoftColor  = {(u32)0xC9CFD2FF};
const Gfx::Color TextMutedColor = {(u32)0x9AA3A8FF};
const Gfx::Color AccentColor    = {(u32)0x7FD65CFF};
const Gfx::Color MelonRedColor  = {(u32)0xE03548FF};
const Gfx::Color OverlayColor   = {(u32)0x0F1114CC};

#endif
