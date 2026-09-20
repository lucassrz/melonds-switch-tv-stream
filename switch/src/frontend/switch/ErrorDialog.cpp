#include "ErrorDialog.h"

#include "Gfx.h"
#include "BoxGui.h"
#include "Style.h"
#include "KeyExplanations.h"

namespace ErrorDialog
{

void Open(const std::string& message)
{
    struct Dialog
    {
        std::string Message;
        double StartTimestamp;
        double EndTimestamp = -INFINITY;

        bool operator()(BoxGui::Frame& rootFrame)
        {
            Gfx::Color color = OverlayColor;
            // fade in
            color.A = (float)std::min((Gfx::AnimationTimestamp - StartTimestamp) * 5.0, 0.8);
            Gfx::DrawRectangle(rootFrame.Area.Position, rootFrame.Area.Size, color);

            Gfx::Vector2f size = {std::min(rootFrame.Area.Size.X * 0.7f, 720.f), 220.f};
            BoxGui::Frame dialogFrame{rootFrame, rootFrame.Area.CenteredChild(size)};
            Gfx::DrawRoundedRect(dialogFrame.Area.Position, dialogFrame.Area.Size, CardColor, UIRadiusLarge);
            Gfx::DrawRoundedOutline(dialogFrame.Area.Position, dialogFrame.Area.Size, BorderColor, UIRadiusLarge, 1.f);

            const float pad = 28.f;
            Gfx::DrawText(Gfx::SystemFontStandard,
                dialogFrame.Area.Position + Gfx::Vector2f{pad, pad}, TextLineHeight * 1.3f,
                TextColor, Gfx::align_Left, Gfx::align_Left, "Something went wrong");
            Gfx::DrawText(Gfx::SystemFontStandard,
                dialogFrame.Area.Position + Gfx::Vector2f{pad, pad + TextLineHeight * 2.2f}, TextLineHeight,
                TextSoftColor, Gfx::align_Left, Gfx::align_Left, Message.c_str());

            // I know that's cheating, but we don't need an InputElement if there's only one element
            Gfx::Vector2f buttonSize = {120.f, 48.f};
            Gfx::Vector2f buttonPos = dialogFrame.Area.Position + dialogFrame.Area.Size - buttonSize - Gfx::Vector2f{pad, pad};
            Gfx::DrawRoundedRect(buttonPos, buttonSize, AccentColor, UIRadius);
            Gfx::DrawText(Gfx::SystemFontStandard, buttonPos + buttonSize * 0.5f, TextLineHeight,
                BgColor, Gfx::align_Center, Gfx::align_Center, "OK");

            KeyExplanation::Explain(KeyExplanation::button_A, "OK");

            const double fadeoutLength = 0.25;
            if (BoxGui::ConfirmPressed())
                EndTimestamp = 0.f;

            return EndTimestamp < 0.0 || Gfx::AnimationTimestamp - EndTimestamp < fadeoutLength;
        }
    };
    BoxGui::OpenModalDialog(Dialog{message});
}

}
