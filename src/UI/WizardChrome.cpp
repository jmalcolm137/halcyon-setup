/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WizardChrome.h"
#include <LibGUI/Painter.h>
#include <LibGfx/Palette.h>
#include <LibGfx/StylePainter.h>

namespace Halcyon {

WizardHeader::WizardHeader()
{
    set_fixed_height(64);
}

void WizardHeader::set_subtitle(ByteString const& subtitle)
{
    m_subtitle = subtitle;
    update();
}

void WizardHeader::set_icon(RefPtr<Gfx::Bitmap const> icon)
{
    m_icon = move(icon);
    update();
}

void WizardHeader::paint_event(GUI::PaintEvent& event)
{
    GUI::Painter painter(*this);
    painter.add_clip_rect(event.rect());
    painter.fill_rect(rect(), Gfx::Color::from_rgb(0xffffff));

    auto title_font = font().with_size(17);
    auto subtitle_font = font().with_size(9);

    int text_left = 16;
    if (m_icon) {
        Gfx::IntRect icon_rect { 14, 9, 44, 44 };
        painter.draw_scaled_bitmap(icon_rect, *m_icon, m_icon->rect(), 1.0f, Gfx::ScalingMode::SmoothPixels);
        text_left = 68;
    }

    painter.draw_text(Gfx::IntRect { text_left, 7, width() - text_left - 8, 26 }, "Halcyon Linux Setup"sv,
        *title_font, Gfx::TextAlignment::CenterLeft, Gfx::Color::from_rgb(0x000000));
    if (!m_subtitle.is_empty()) {
        painter.draw_text(Gfx::IntRect { text_left, 31, width() - text_left - 8, 18 }, m_subtitle,
            *subtitle_font, Gfx::TextAlignment::CenterLeft, Gfx::Color::from_rgb(0x404040));
    }

    painter.fill_rect(Gfx::IntRect { 0, height() - 4, width(), 4 }, Gfx::Color::from_rgb(0x000080));
    painter.fill_rect(Gfx::IntRect { 0, height() - 1, width(), 1 }, Gfx::Color::from_rgb(0x808080));
}

WizardSidebar::WizardSidebar()
{
    set_fixed_width(178);
}

void WizardSidebar::set_steps(Vector<ByteString> steps)
{
    m_steps = move(steps);
    update();
}

void WizardSidebar::set_current(int current)
{
    m_current = current;
    update();
}

void WizardSidebar::paint_event(GUI::PaintEvent& event)
{
    GUI::Painter painter(*this);
    painter.add_clip_rect(event.rect());
    painter.fill_rect(rect(), Gfx::Color::from_rgb(0x000080));
    painter.fill_rect(Gfx::IntRect { width() - 3, 0, 3, height() }, Gfx::Color::from_rgb(0x808080));

    // A little Windows 95-era beveled product mark.
    Gfx::IntRect mark { 14, 12, 46, 46 };
    painter.fill_rect(mark, Gfx::Color::from_rgb(0x0000a8));
    painter.draw_line({ mark.left(), mark.top() }, { mark.right(), mark.top() }, Gfx::Color::from_rgb(0xffffff));
    painter.draw_line({ mark.left(), mark.top() }, { mark.left(), mark.bottom() }, Gfx::Color::from_rgb(0xffffff));
    painter.draw_line({ mark.left(), mark.bottom() }, { mark.right(), mark.bottom() }, Gfx::Color::from_rgb(0x000040));
    painter.draw_line({ mark.right(), mark.top() }, { mark.right(), mark.bottom() }, Gfx::Color::from_rgb(0x000040));
    auto logo_font = font().with_size(22);
    painter.draw_text(mark, "H"sv, *logo_font, Gfx::TextAlignment::Center, Gfx::Color::from_rgb(0xffff00));

    auto title_font = font().with_size(13);
    painter.draw_text(Gfx::IntRect { 70, 10, width() - 78, 20 }, "Halcyon"sv, *title_font,
        Gfx::TextAlignment::CenterLeft, Gfx::Color::from_rgb(0xffffff));
    auto subtitle_font = font().with_size(8);
    painter.draw_text(Gfx::IntRect { 70, 29, width() - 78, 15 }, "Linux Setup"sv, *subtitle_font,
        Gfx::TextAlignment::CenterLeft, Gfx::Color::from_rgb(0xc0c0ff));

    painter.draw_line({ 10, 70 }, { width() - 12, 70 }, Gfx::Color::from_rgb(0x8080ff));

    int y = 84;
    for (size_t index = 0; index < m_steps.size(); ++index) {
        auto const& step = m_steps[index];
        Gfx::IntRect row { 8, y, width() - 20, 24 };
        bool is_current = static_cast<int>(index) == m_current;
        bool is_done = static_cast<int>(index) < m_current;

        if (is_current) {
            painter.fill_rect(row, Gfx::Color::from_rgb(0x0000a8));
            painter.draw_rect(row, Gfx::Color::from_rgb(0xffffff));
            painter.draw_text(Gfx::IntRect { row.x() + 8, row.y(), 14, row.height() }, ">"sv,
                Gfx::TextAlignment::CenterLeft, Gfx::Color::from_rgb(0xffff00));
        }

        Gfx::IntRect marker { row.x() + 8, row.y() + 6, 12, 12 };
        if (is_done) {
            painter.draw_line({ marker.left() + 1, marker.top() + 6 }, { marker.left() + 4, marker.top() + 10 }, Gfx::Color::from_rgb(0x00ff00), 2);
            painter.draw_line({ marker.left() + 4, marker.top() + 10 }, { marker.left() + 11, marker.top() + 1 }, Gfx::Color::from_rgb(0x00ff00), 2);
        } else if (!is_current) {
            painter.draw_rect(marker, Gfx::Color::from_rgb(0x8080ff));
        }

        auto color = is_current ? Gfx::Color::from_rgb(0xffff00)
            : is_done                  ? Gfx::Color::from_rgb(0xc8c8ff)
                                       : Gfx::Color::from_rgb(0x9090d0);
        painter.draw_text(Gfx::IntRect { row.x() + 26, row.y(), row.width() - 28, row.height() }, step,
            Gfx::TextAlignment::CenterLeft, color);
        y += 27;
    }

    painter.draw_text(Gfx::IntRect { 10, height() - 34, width() - 20, 24 }, "Halcyon Linux 1.0"sv,
        Gfx::TextAlignment::CenterLeft, Gfx::Color::from_rgb(0x9090d0));
}

WizardFooter::WizardFooter()
{
    set_fixed_height(58);
}

void WizardFooter::paint_event(GUI::PaintEvent& event)
{
    GUI::Painter painter(*this);
    painter.add_clip_rect(event.rect());
    painter.fill_rect(rect(), palette().color(Gfx::ColorRole::Window));
    painter.draw_line({ 0, 0 }, { width(), 0 }, palette().color(Gfx::ColorRole::ThreedHighlight));
    painter.draw_line({ 0, 1 }, { width(), 1 }, palette().color(Gfx::ColorRole::ThreedShadow1));
}

}
