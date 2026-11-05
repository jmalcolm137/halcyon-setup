/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Vector.h>
#include <LibGUI/Model.h>

namespace Halcyon {

struct MountRow {
    ByteString partition;
    u64 size_bytes { 0 };
    ByteString fs_type;
    ByteString label;
    ByteString mountpoint;
    bool format { true };
};

class MountTableModel final : public GUI::Model {
public:
    enum Column {
        Device = 0,
        Size,
        Filesystem,
        Label,
        MountPoint,
        Format,
        __Count,
    };

    static NonnullRefPtr<MountTableModel> create()
    {
        return adopt_ref(*new MountTableModel);
    }

    virtual int row_count(GUI::ModelIndex const& = GUI::ModelIndex()) const override { return m_rows.size(); }
    virtual int column_count(GUI::ModelIndex const& = GUI::ModelIndex()) const override { return Column::__Count; }
    virtual ErrorOr<String> column_name(int) const override;
    virtual GUI::Variant data(GUI::ModelIndex const&, GUI::ModelRole) const override;

    void set_rows(Vector<MountRow> rows);
    Vector<MountRow> const& rows() const { return m_rows; }
    MountRow* row_at(int index);

private:
    MountTableModel() = default;

    Vector<MountRow> m_rows;
};

class UserTableModel final : public GUI::Model {
public:
    enum Column {
        Username = 0,
        FullName,
        Groups,
        __Count,
    };

    static NonnullRefPtr<UserTableModel> create()
    {
        return adopt_ref(*new UserTableModel);
    }

    virtual int row_count(GUI::ModelIndex const& = GUI::ModelIndex()) const override { return m_usernames.size(); }
    virtual int column_count(GUI::ModelIndex const& = GUI::ModelIndex()) const override { return Column::__Count; }
    virtual ErrorOr<String> column_name(int) const override;
    virtual GUI::Variant data(GUI::ModelIndex const&, GUI::ModelRole) const override;

    void set_users(Vector<ByteString> usernames, Vector<ByteString> full_names, Vector<ByteString> groups);
    int count() const { return m_usernames.size(); }

private:
    UserTableModel() = default;

    Vector<ByteString> m_usernames;
    Vector<ByteString> m_full_names;
    Vector<ByteString> m_groups;
};

}
