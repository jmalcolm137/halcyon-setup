/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WorldMap.h"
#include <LibGUI/Event.h>
#include <LibGUI/Painter.h>
#include <LibGfx/Palette.h>
#include <LibGfx/ScalingMode.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace Halcyon {

static RefPtr<Gfx::Bitmap const> load_world_map()
{
    if (auto const* path = getenv("HALCYON_SETUP_MAP"); path && *path) {
        auto bitmap = Gfx::Bitmap::load_from_file(StringView { path, strlen(path) });
        if (!bitmap.is_error())
            return bitmap.release_value();
    }

    auto installed = Gfx::Bitmap::load_from_file("/usr/share/halcyon-setup/world-map.png"sv);
    if (!installed.is_error())
        return installed.release_value();

    auto resource = Gfx::Bitmap::load_from_uri("resource://graphics/halcyon-world-map.png"sv);
    if (!resource.is_error())
        return resource.release_value();

    auto local = Gfx::Bitmap::load_from_file("res/world-map.png"sv);
    if (!local.is_error())
        return local.release_value();

    return {};
}

WorldMap::WorldMap()
{
    m_bitmap = load_world_map();
    set_min_size(300, 170);
    set_preferred_size(560, 280);
}

void WorldMap::set_zones(Vector<ZonePoint> zones)
{
    m_zones = move(zones);
    update();
}

void WorldMap::set_timezone(ByteString const& timezone)
{
    m_timezone = timezone;
    update();
}

Gfx::IntRect WorldMap::map_rect() const
{
    auto available = rect().shrunken(4, 4);
    if (!m_bitmap || available.is_empty())
        return available;

    auto bitmap_size = m_bitmap->size();
    if (bitmap_size.width() == 0 || bitmap_size.height() == 0)
        return available;

    auto scale = min(static_cast<double>(available.width()) / bitmap_size.width(), static_cast<double>(available.height()) / bitmap_size.height());
    auto width = max(1, static_cast<int>(bitmap_size.width() * scale));
    auto height = max(1, static_cast<int>(bitmap_size.height() * scale));
    Gfx::IntRect result { Gfx::IntPoint { 0, 0 }, Gfx::IntSize { width, height } };
    result.center_within(available);
    return result;
}

Optional<Gfx::IntPoint> WorldMap::point_for(ZonePoint const& zone) const
{
    auto area = map_rect();
    if (area.is_empty())
        return {};

    auto x = area.left() + static_cast<int>(((zone.longitude + 180.0) / 360.0) * area.width());
    auto y = area.top() + static_cast<int>(((90.0 - zone.latitude) / 180.0) * area.height());
    return Gfx::IntPoint { x, y };
}

void WorldMap::paint_event(GUI::PaintEvent& event)
{
    GUI::Painter painter(*this);
    painter.add_clip_rect(event.rect());

    painter.fill_rect(rect(), palette().color(Gfx::ColorRole::Base));
    auto area = map_rect();
    painter.draw_rect(area, palette().color(Gfx::ColorRole::ThreedShadow1));

    if (!m_bitmap) {
        painter.draw_text(area, "World map image is not available."sv, Gfx::TextAlignment::Center,
            palette().color(Gfx::ColorRole::DisabledText));
        return;
    }

    painter.draw_scaled_bitmap(area, *m_bitmap, m_bitmap->rect(), 1.0f, Gfx::ScalingMode::SmoothPixels);

    auto selected = m_timezone;
    for (size_t index = 0; index < m_zones.size(); ++index) {
        auto const& zone = m_zones[index];
        auto maybe_point = point_for(zone);
        if (!maybe_point.has_value())
            continue;
        auto point = maybe_point.value();
        if (!area.contains(point))
            continue;

        auto is_selected = zone.name == selected;
        if (is_selected) {
            painter.fill_rect(Gfx::IntRect { point.x() - 4, point.y() - 4, 9, 9 }, Gfx::Color::from_rgb(0x800000));
            painter.fill_rect(Gfx::IntRect { point.x() - 2, point.y() - 2, 5, 5 }, Gfx::Color::from_rgb(0xff2020));
        } else if (static_cast<int>(index) == m_hovered_zone) {
            painter.fill_rect(Gfx::IntRect { point.x() - 2, point.y() - 2, 5, 5 }, palette().color(Gfx::ColorRole::Selection));
        } else {
            painter.fill_rect(Gfx::IntRect { point.x(), point.y(), 2, 2 }, Gfx::Color::from_rgb(0x505050));
        }
    }

    for (auto const& zone : m_zones) {
        if (zone.name != selected)
            continue;
        auto maybe_point = point_for(zone);
        if (!maybe_point.has_value())
            continue;
        auto point = maybe_point.value();
        Gfx::IntRect label { point.x() + 10, point.y() - 12, 190, 24 };
        if (label.right() > area.right())
            label.set_x(point.x() - label.width() - 10);
        if (label.top() < area.top())
            label.set_y(point.y() + 10);
        painter.fill_rect(label, Gfx::Color::from_rgb(0xffffe1));
        painter.draw_rect(label, Gfx::Color::from_rgb(0x000080));
        painter.draw_text(label, zone.name, Gfx::TextAlignment::Center, Gfx::Color::from_rgb(0x000000));
        break;
    }
}

WorldMap::ZonePoint const* WorldMap::nearest_zone(Gfx::IntPoint position) const
{
    auto area = map_rect();
    if (!area.contains(position) || m_zones.is_empty())
        return nullptr;

    auto longitude = ((static_cast<double>(position.x() - area.left()) / area.width()) * 360.0) - 180.0;
    auto latitude = 90.0 - ((static_cast<double>(position.y() - area.top()) / area.height()) * 180.0);

    ZonePoint const* best = nullptr;
    double best_distance = 1.0e30;
    for (auto const& zone : m_zones) {
        auto longitude_delta = fabs(longitude - zone.longitude);
        if (longitude_delta > 180.0)
            longitude_delta = 360.0 - longitude_delta;
        auto latitude_delta = fabs(latitude - zone.latitude);
        auto longitude_weight = cos(latitude * M_PI / 180.0);
        auto distance = latitude_delta * latitude_delta + longitude_delta * longitude_weight * longitude_delta * longitude_weight;
        if (distance < best_distance) {
            best_distance = distance;
            best = &zone;
        }
    }
    return best;
}

void WorldMap::mousedown_event(GUI::MouseEvent& event)
{
    if (event.button() != GUI::MouseButton::Primary)
        return;

    auto const* zone = nearest_zone(event.position());
    if (!zone)
        return;

    m_timezone = zone->name;
    update();
    if (on_zone_selected)
        on_zone_selected(zone->name);
}

void WorldMap::mousemove_event(GUI::MouseEvent& event)
{
    auto const* zone = nearest_zone(event.position());
    int index = -1;
    if (zone) {
        for (size_t i = 0; i < m_zones.size(); ++i) {
            if (&m_zones[i] == zone) {
                index = static_cast<int>(i);
                break;
            }
        }
    }

    if (index != m_hovered_zone) {
        m_hovered_zone = index;
        if (zone)
            set_tooltip(String::from_utf8_with_replacement_character(zone->name.view()));
        else
            set_tooltip({});
        update();
    }
}

void WorldMap::leave_event(Core::Event& event)
{
    if (m_hovered_zone != -1) {
        m_hovered_zone = -1;
        update();
    }
    GUI::Widget::leave_event(event);
}

}
