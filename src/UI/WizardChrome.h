/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Vector.h>
#include <LibGUI/Widget.h>
#include <LibGfx/Bitmap.h>

namespace Halcyon {

class WizardHeader final : public GUI::Widget {
    C_OBJECT(WizardHeader)

public:
    virtual ~WizardHeader() override = default;

    void set_subtitle(ByteString const&);
    void set_icon(RefPtr<Gfx::Bitmap const>);

private:
    WizardHeader();

    virtual void paint_event(GUI::PaintEvent&) override;

    ByteString m_subtitle;
    RefPtr<Gfx::Bitmap const> m_icon;
};

class WizardSidebar final : public GUI::Widget {
    C_OBJECT(WizardSidebar)

public:
    virtual ~WizardSidebar() override = default;

    void set_steps(Vector<ByteString>);
    void set_current(int);

private:
    WizardSidebar();

    virtual void paint_event(GUI::PaintEvent&) override;

    Vector<ByteString> m_steps;
    int m_current { 0 };
};

class WizardFooter final : public GUI::Widget {
    C_OBJECT(WizardFooter)

public:
    virtual ~WizardFooter() override = default;

private:
    WizardFooter();

    virtual void paint_event(GUI::PaintEvent&) override;
};

}
