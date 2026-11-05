/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Function.h>
#include <AK/Optional.h>
#include <AK/Vector.h>
#include <LibGUI/Widget.h>
#include <LibGfx/Bitmap.h>

namespace Halcyon {

class WorldMap final : public GUI::Widget {
    C_OBJECT(WorldMap)

public:
    struct ZonePoint {
        ByteString name;
        double latitude { 0 };
        double longitude { 0 };
    };

    virtual ~WorldMap() override = default;

    void set_zones(Vector<ZonePoint>);
    void set_timezone(ByteString const&);

    Function<void(ByteString const&)> on_zone_selected;

private:
    WorldMap();

    virtual void paint_event(GUI::PaintEvent&) override;
    virtual void mousedown_event(GUI::MouseEvent&) override;
    virtual void mousemove_event(GUI::MouseEvent&) override;
    virtual void leave_event(Core::Event&) override;

    Gfx::IntRect map_rect() const;
    Optional<Gfx::IntPoint> point_for(ZonePoint const&) const;
    ZonePoint const* nearest_zone(Gfx::IntPoint) const;

    RefPtr<Gfx::Bitmap const> m_bitmap;
    Vector<ZonePoint> m_zones;
    ByteString m_timezone;
    int m_hovered_zone { -1 };
};

}
