/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Dialogs.h"
#include <AK/NumberFormat.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/Button.h>
#include <LibGUI/CheckBox.h>
#include <LibGUI/ComboBox.h>
#include <LibGUI/Dialog.h>
#include <LibGUI/InputBox.h>
#include <LibGUI/ItemListModel.h>
#include <LibGUI/Label.h>
#include <LibGUI/MessageBox.h>
#include <LibGUI/TextBox.h>
#include <LibGUI/Widget.h>
#include <LibGfx/SystemTheme.h>

namespace Halcyon {

static String to_string(StringView value)
{
    return String::from_utf8_with_replacement_character(value);
}

static GUI::Widget& add_labeled_row(GUI::Widget& parent, StringView label, int label_width)
{
    auto& row = parent.add<GUI::Widget>();
    row.set_layout<GUI::HorizontalBoxLayout>(GUI::Margins {}, 8);
    auto& text = row.add<GUI::Label>(to_string(label));
    text.set_fixed_width(label_width);
    text.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    return row;
}

class UserDialog final : public GUI::Dialog {
    C_OBJECT(UserDialog)

public:
    Optional<UserAccount> account() const { return m_account; }

private:
    UserDialog(GUI::Window& parent)
        : GUI::Dialog(&parent)
    {
        set_title("Add User Account"sv);
        resize(430, 360);
        set_resizable(false);

        auto main_ref = set_main_widget<GUI::Widget>();
        auto& main = *main_ref;
        main.set_fill_with_background_color(true);
        main.set_layout<GUI::VerticalBoxLayout>(GUI::Margins { 14 }, 7);

        auto& heading = main.add<GUI::Label>("Create a user account for the new system."_string);
        heading.set_font_weight(700);
        heading.set_text_alignment(Gfx::TextAlignment::CenterLeft);

        auto& name_row = add_labeled_row(main, "User name:"sv, 92);
        m_username = name_row.add<GUI::TextBox>();
        m_username->set_placeholder("halcyon"_string);

        auto& full_row = add_labeled_row(main, "Full name:"sv, 92);
        m_full_name = full_row.add<GUI::TextBox>();
        m_full_name->set_placeholder("Halcyon User"_string);

        auto& password_row = add_labeled_row(main, "Password:"sv, 92);
        m_password = password_row.add<GUI::PasswordBox>();

        auto& confirm_row = add_labeled_row(main, "Confirm:"sv, 92);
        m_confirm = confirm_row.add<GUI::PasswordBox>();

        auto& groups_label = main.add<GUI::Label>("Groups:"_string);
        groups_label.set_text_alignment(Gfx::TextAlignment::CenterLeft);

        auto& groups_row = main.add<GUI::Widget>();
        groups_row.set_layout<GUI::HorizontalBoxLayout>(GUI::Margins {}, 12);
        m_wheel = groups_row.add<GUI::CheckBox>("wheel (administrator)"_string);
        m_wheel->set_checked(true);
        m_kvm = groups_row.add<GUI::CheckBox>("kvm"_string);
        m_kvm->set_checked(true);
        m_plugdev = groups_row.add<GUI::CheckBox>("plugdev"_string);
        m_plugdev->set_checked(true);

        main.add_spacer();

        auto& buttons = main.add<GUI::Widget>();
        buttons.set_layout<GUI::HorizontalBoxLayout>(GUI::Margins {}, 8);
        buttons.add_spacer();
        auto& cancel = buttons.add<GUI::Button>("Cancel"_string);
        cancel.set_fixed_width(84);
        cancel.on_click = [this](unsigned) {
            done(GUI::Dialog::ExecResult::Cancel);
        };
        auto& ok = buttons.add<GUI::Button>("Add User"_string);
        ok.set_fixed_width(96);
        ok.set_default(true);
        ok.on_click = [this](unsigned) {
            accept();
        };
    }

    void accept()
    {
        auto username = m_username->text();
        if (username.is_empty()) {
            GUI::MessageBox::show(this, "Enter a user name."sv, "Add User Account"sv, GUI::MessageBox::Type::Warning);
            return;
        }
        if (m_password->text() != m_confirm->text()) {
            GUI::MessageBox::show(this, "The passwords do not match."sv, "Add User Account"sv, GUI::MessageBox::Type::Warning);
            return;
        }
        if (m_password->text().is_empty()) {
            GUI::MessageBox::show(this, "Enter a password for the user."sv, "Add User Account"sv, GUI::MessageBox::Type::Warning);
            return;
        }

        UserAccount user;
        user.username = username;
        user.full_name = m_full_name->text();
        user.password = m_password->text();
        if (m_wheel->is_checked())
            user.groups.append("wheel"sv);
        if (m_kvm->is_checked())
            user.groups.append("kvm"sv);
        if (m_plugdev->is_checked())
            user.groups.append("plugdev"sv);

        m_account = move(user);
        done(GUI::Dialog::ExecResult::OK);
    }

    Optional<UserAccount> m_account;
    RefPtr<GUI::TextBox> m_username;
    RefPtr<GUI::TextBox> m_full_name;
    RefPtr<GUI::PasswordBox> m_password;
    RefPtr<GUI::PasswordBox> m_confirm;
    RefPtr<GUI::CheckBox> m_wheel;
    RefPtr<GUI::CheckBox> m_kvm;
    RefPtr<GUI::CheckBox> m_plugdev;
};

Optional<UserAccount> show_add_user_dialog(GUI::Window& parent)
{
    auto dialog = UserDialog::construct(parent);
    if (dialog->exec() != GUI::Dialog::ExecResult::OK)
        return {};
    return dialog->account();
}

Optional<ByteString> show_mountpoint_dialog(GUI::Window& parent, ByteString const& partition, u64 size_bytes, ByteString const& current)
{
    String value = String::from_utf8_with_replacement_character(current.view());
    auto prompt = ByteString::formatted(
        "Mount point for {} ({}).\n\nUse / for the root filesystem, /boot, /boot/efi, /home or swap.\nLeave empty to clear the mount point.",
        partition,
        human_readable_size(size_bytes).to_byte_string());

    while (true) {
        auto result = GUI::InputBox::show(&parent, value, prompt, "Set Mount Point"_string,
            GUI::InputType::Text, "/boot"_string);
        if (result != GUI::Dialog::ExecResult::OK)
            return {};

        auto mountpoint = value.to_byte_string();
        if (mountpoint.is_empty())
            return mountpoint;
        if (mountpoint == "swap"sv || mountpoint.starts_with('/'))
            return mountpoint;

        GUI::MessageBox::show(&parent, "Enter an absolute path such as /boot, or enter swap."sv,
            "Set Mount Point"sv, GUI::MessageBox::Type::Warning);
    }
}

class FormatDialog final : public GUI::Dialog {
    C_OBJECT(FormatDialog)

public:
    MountAssignment result() const { return m_result; }

private:
    FormatDialog(GUI::Window& parent, MountAssignment const& assignment)
        : GUI::Dialog(&parent)
        , m_result(assignment)
    {
        set_title("Format Partition"sv);
        resize(420, 270);
        set_resizable(false);

        auto main_ref = set_main_widget<GUI::Widget>();
        auto& main = *main_ref;
        main.set_fill_with_background_color(true);
        main.set_layout<GUI::VerticalBoxLayout>(GUI::Margins { 14 }, 8);

        auto& heading = main.add<GUI::Label>(MUST(String::formatted("Format {}", assignment.partition)));
        heading.set_font_weight(700);
        heading.set_text_alignment(Gfx::TextAlignment::CenterLeft);

        auto& warning = main.add<GUI::Label>("All data on this partition will be erased."_string);
        warning.set_text_alignment(Gfx::TextAlignment::CenterLeft);
        warning.set_foreground_role(Gfx::ColorRole::Red);

        auto& fs_row = add_labeled_row(main, "Filesystem:"sv, 90);
        // ItemListModel keeps a reference to the container; the dialog owns it
        // so the model does not point at a destroyed local vector.
        m_file_systems = {
            "bcachefs"sv, "ext4"sv, "btrfs"sv, "xfs"sv, "f2fs"sv, "vfat"sv, "swap"sv,
        };
        m_fs = fs_row.add<GUI::ComboBox>();
        m_fs->set_only_allow_values_from_model(true);
        m_fs->set_model(*GUI::ItemListModel<ByteString>::create(m_file_systems));
        m_fs->set_text(assignment.fs_type.is_empty() ? ByteString { "bcachefs" } : assignment.fs_type);

        auto& label_row = add_labeled_row(main, "Label:"sv, 90);
        m_label = label_row.add<GUI::TextBox>();
        m_label->set_text(assignment.label);

        main.add_spacer();

        auto& buttons = main.add<GUI::Widget>();
        buttons.set_layout<GUI::HorizontalBoxLayout>(GUI::Margins {}, 8);
        buttons.add_spacer();
        auto& cancel = buttons.add<GUI::Button>("Cancel"_string);
        cancel.set_fixed_width(84);
        cancel.on_click = [this](unsigned) {
            done(GUI::Dialog::ExecResult::Cancel);
        };
        auto& ok = buttons.add<GUI::Button>("Format"_string);
        ok.set_fixed_width(84);
        ok.set_default(true);
        ok.on_click = [this](unsigned) {
            m_result.fs_type = m_fs->text();
            m_result.label = m_label->text();
            m_result.format = true;
            if (m_result.mountpoint == "/"sv && m_result.fs_type == "swap"sv) {
                GUI::MessageBox::show(this, "The root filesystem cannot be swap."sv, "Format Partition"sv, GUI::MessageBox::Type::Warning);
                return;
            }
            done(GUI::Dialog::ExecResult::OK);
        };
    }

    MountAssignment m_result;
    Vector<ByteString> m_file_systems;
    RefPtr<GUI::ComboBox> m_fs;
    RefPtr<GUI::TextBox> m_label;
};

Optional<MountAssignment> show_format_dialog(GUI::Window& parent, MountAssignment const& assignment)
{
    auto dialog = FormatDialog::construct(parent, assignment);
    if (dialog->exec() != GUI::Dialog::ExecResult::OK)
        return {};
    return dialog->result();
}

}
