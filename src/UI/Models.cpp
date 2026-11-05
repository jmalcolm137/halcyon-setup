/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Models.h"
#include "../Installer/Config.h"
#include <AK/NumberFormat.h>

namespace Halcyon {

ErrorOr<String> MountTableModel::column_name(int column) const
{
    switch (column) {
    case Column::Device:
        return "Partition"_string;
    case Column::Size:
        return "Size"_string;
    case Column::Filesystem:
        return "Filesystem"_string;
    case Column::Label:
        return "Label"_string;
    case Column::MountPoint:
        return "Mount point"_string;
    case Column::Format:
        return "Format"_string;
    default:
        return String {};
    }
}

GUI::Variant MountTableModel::data(GUI::ModelIndex const& index, GUI::ModelRole role) const
{
    if (index.row() < 0 || static_cast<size_t>(index.row()) >= m_rows.size())
        return {};

    auto const& row = m_rows[index.row()];
    if (role == GUI::ModelRole::TextAlignment)
        return index.column() == Column::Size ? Gfx::TextAlignment::CenterRight : Gfx::TextAlignment::CenterLeft;

    if (role == GUI::ModelRole::Display || role == GUI::ModelRole::Sort) {
        switch (index.column()) {
        case Column::Device:
            return row.partition;
        case Column::Size:
            return human_readable_size(row.size_bytes).to_byte_string();
        case Column::Filesystem:
            return filesystem_display_name(row.fs_type);
        case Column::Label:
            return row.label;
        case Column::MountPoint:
            return row.mountpoint;
        case Column::Format:
            return row.format ? "Yes"sv : "No"sv;
        default:
            return {};
        }
    }
    return {};
}

void MountTableModel::set_rows(Vector<MountRow> rows)
{
    m_rows = move(rows);
    did_update();
}

MountRow* MountTableModel::row_at(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= m_rows.size())
        return nullptr;
    return &m_rows[index];
}

ErrorOr<String> UserTableModel::column_name(int column) const
{
    switch (column) {
    case Column::Username:
        return "User name"_string;
    case Column::FullName:
        return "Full name"_string;
    case Column::Groups:
        return "Groups"_string;
    default:
        return String {};
    }
}

GUI::Variant UserTableModel::data(GUI::ModelIndex const& index, GUI::ModelRole role) const
{
    if (index.row() < 0 || static_cast<size_t>(index.row()) >= m_usernames.size())
        return {};

    if (role == GUI::ModelRole::TextAlignment)
        return Gfx::TextAlignment::CenterLeft;

    if (role == GUI::ModelRole::Display || role == GUI::ModelRole::Sort) {
        switch (index.column()) {
        case Column::Username:
            return m_usernames[index.row()];
        case Column::FullName:
            return m_full_names[index.row()];
        case Column::Groups:
            return m_groups[index.row()];
        default:
            return {};
        }
    }
    return {};
}

void UserTableModel::set_users(Vector<ByteString> usernames, Vector<ByteString> full_names, Vector<ByteString> groups)
{
    m_usernames = move(usernames);
    m_full_names = move(full_names);
    m_groups = move(groups);
    did_update();
}

}
