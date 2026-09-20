#include "BackButton.h"

#include "KeyExplanations.h"
#include "Style.h"
#include "main.h"

namespace BackButton
{

void DoGui(BoxGui::Frame& parent, const char* title)
{
    const float buttonSize = 44.f;
    const float margin = UIPagePadding;

    BoxGui::Frame backButton{parent, {{margin, (BackButtonHeight - buttonSize) / 2.f}, {buttonSize, buttonSize}}};
    bool selected = BoxGui::InputElement(backButton, BoxGui::MakeUniqueName("backbutton", 0));

    if (selected)
    {
        KeyExplanation::Explain(KeyExplanation::button_A, "Back");
        if (BoxGui::ConfirmPressed())
            GoBack();
    }

    // opaque header so scrolled content never shows through
    Gfx::DrawRectangle({0.f, 0.f}, {parent.Area.Size.X, BackButtonHeight}, BgColor);
    Gfx::DrawRoundedRect(backButton.Area.Position, backButton.Area.Size, selected ? AccentColor : RaisedColor, UIRadius);
    Gfx::DrawText(Gfx::SystemFontNintendoExt, backButton.Area.Position + backButton.Area.Size * 0.5f, TextLineHeight * 1.2f,
        selected ? BgColor : TextColor, Gfx::align_Center, Gfx::align_Center, GFX_NINTENDOFONT_BACK);

    Gfx::DrawText(Gfx::SystemFontStandard,
        {margin + buttonSize + 18.f, BackButtonHeight / 2.f}, TextLineHeight * 1.5f,
        TextColor, Gfx::align_Left, Gfx::align_Center, title);

    Gfx::DrawRectangle({margin, BackButtonHeight - 1.f}, {parent.Area.Size.X - 2.f * margin, 1.f}, BorderColor);
}

void GoBack()
{
    CurrentUiScreen = uiScreen_Start;
}

}
