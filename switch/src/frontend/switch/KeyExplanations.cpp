#include "KeyExplanations.h"

#include "Style.h"

#include <string>

namespace KeyExplanation
{

int Hold = 0;
u32 KeysExplained, KeysExplainedThisFrame;
std::string KeyExplanations[buttons_Count];

void Explain(int button, const char* explanation)
{
    Hold = 2;
    KeysExplainedThisFrame |= 1 << button;
    KeyExplanations[button] = explanation;
}

void DoGui(BoxGui::Frame& parent)
{
    // hack: when switching between gui states it can happen that the currently selected items is reset
    // so for one frame there's no item selected which results in a small flicker because there's no key explanation
    if (KeysExplainedThisFrame == 0)
    {
        Hold--;
        if (Hold < 0)
            KeysExplained = 0;
    }
    else
    {
        KeysExplained = KeysExplainedThisFrame;
    }
    KeysExplainedThisFrame = 0;

    if (KeysExplained == 0)
        return;

    const float pillHeight = 36.f;
    const float margin = 24.f;
    const float gap = 10.f;

    u32 keysExplained = KeysExplained;
    BoxGui::Skewer skewer{parent, parent.Area.Size.Y - margin - pillHeight / 2.f, BoxGui::direction_Horizontal};
    skewer.AlignRight(margin);
    while (keysExplained)
    {
        int button = 31 - __builtin_clz(keysExplained);
        keysExplained &= ~(1 << button);

        const char* buttonIcon = "";
        switch (button)
        {
        case button_A: buttonIcon = GFX_NINTENDOFONT_A_BUTTON; break;
        case button_B: buttonIcon = GFX_NINTENDOFONT_B_BUTTON; break;
        case button_X: buttonIcon = GFX_NINTENDOFONT_X_BUTTON; break;
        case button_Y: buttonIcon = GFX_NINTENDOFONT_Y_BUTTON; break;
        case button_Plus: buttonIcon = GFX_NINTENDOFONT_PLUS_BUTTON; break;
        case button_Minus: buttonIcon = GFX_NINTENDOFONT_MINUS_BUTTON; break;
        default: break;
        }

        const char* explanation = KeyExplanations[button].c_str();
        Gfx::Vector2f textSize = Gfx::MeasureText(Gfx::SystemFontStandard, TextLineHeight * 0.85f, explanation);
        float pillWidth = 14.f + TextLineHeight + 8.f + textSize.X + 16.f;

        BoxGui::Frame frame{parent, skewer.Spit({pillWidth, pillHeight})};
        Gfx::DrawRoundedRect(frame.Area.Position, frame.Area.Size, RaisedColor, pillHeight / 2.f);
        Gfx::Vector2f textPos = frame.Area.Position + Gfx::Vector2f{14.f, pillHeight / 2.f};
        Gfx::DrawText(Gfx::SystemFontNintendoExt, textPos, TextLineHeight, TextColor,
            Gfx::align_Left, Gfx::align_Center, buttonIcon);
        Gfx::DrawText(Gfx::SystemFontStandard, textPos + Gfx::Vector2f{TextLineHeight + 8.f, 0.f}, TextLineHeight * 0.85f, TextSoftColor,
            Gfx::align_Left, Gfx::align_Center, explanation);
        skewer.Advance(gap);
    }
}

void Reset()
{
    KeysExplainedThisFrame = 0;
}

}
