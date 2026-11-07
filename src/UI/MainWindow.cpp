/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "MainWindow.h"
#include "Dialogs.h"
#include "WizardChrome.h"
#include "WorldMap.h"
#include "../Installer/Command.h"
#include "../Installer/Devices.h"
#include "../Installer/Installer.h"
#include <AK/NumberFormat.h>
#include <AK/StringBuilder.h>
#include <LibCore/Process.h>
#include <LibGUI/Application.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/Button.h>
#include <LibGUI/CheckBox.h>
#include <LibGUI/ComboBox.h>
#include <LibGUI/Frame.h>
#include <LibGUI/GroupBox.h>
#include <LibGUI/ItemListModel.h>
#include <LibGUI/Label.h>
#include <LibGUI/MessageBox.h>
#include <LibGUI/Progressbar.h>
#include <LibGUI/RadioButton.h>
#include <LibGUI/StackWidget.h>
#include <LibGUI/TableView.h>
#include <LibGUI/TextBox.h>
#include <LibGUI/TextEditor.h>
#include <LibGUI/Widget.h>
#include <LibGfx/TextWrapping.h>
#include <LibTimeZone/TimeZone.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

namespace Halcyon {

static String to_string(ByteString const& value)
{
    return String::from_utf8_with_replacement_character(value.view());
}

static String to_string(StringView value)
{
    return String::from_utf8_with_replacement_character(value);
}

static ByteString size_string(u64 bytes)
{
    return human_readable_size(bytes).to_byte_string();
}

static bool hostname_is_valid(StringView hostname)
{
    if (hostname.is_empty() || hostname.length() > 63)
        return false;
    if (hostname[0] == '-' || hostname[hostname.length() - 1] == '-')
        return false;
    for (auto character : hostname) {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '-' || character == '.')
            continue;
        return false;
    }
    return true;
}

static void show_warning(GUI::Window& window, StringView text, StringView title = "Halcyon Linux Setup"sv)
{
    GUI::MessageBox::show(&window, text, title, GUI::MessageBox::Type::Warning);
}

static ByteString keyboard_description(ByteString const& layout)
{
    if (layout == "us"sv)
        return "English (US)"sv;
    if (layout == "gb"sv)
        return "English (UK)"sv;
    if (layout == "de"sv)
        return "German"sv;
    if (layout == "fr"sv)
        return "French"sv;
    if (layout == "es"sv)
        return "Spanish"sv;
    if (layout == "it"sv)
        return "Italian"sv;
    if (layout == "pt"sv)
        return "Portuguese"sv;
    if (layout == "nl"sv)
        return "Dutch"sv;
    if (layout == "se"sv)
        return "Swedish"sv;
    if (layout == "no"sv)
        return "Norwegian"sv;
    if (layout == "dk"sv)
        return "Danish"sv;
    if (layout == "fi"sv)
        return "Finnish"sv;
    if (layout == "pl"sv)
        return "Polish"sv;
    if (layout == "cz"sv)
        return "Czech"sv;
    if (layout == "ru"sv)
        return "Russian"sv;
    if (layout == "tr"sv)
        return "Turkish"sv;
    if (layout == "br"sv)
        return "Portuguese (Brazil)"sv;
    if (layout == "jp"sv)
        return "Japanese"sv;
    return layout;
}

MainWindow::MainWindow(bool demo)
    : m_demo(demo)
    , m_install_timer(Core::Timer::create_repeating(150, [this] {
        poll_install();
    }))
    , m_editor_timer(Core::Timer::create_repeating(500, [this] {
        refresh_after_editor();
    }))
{
}

MainWindow::~MainWindow()
{
    if (m_install_thread) {
        m_cancel_requested.store(true);
        if (m_install_thread->needs_to_be_joined())
            (void)m_install_thread->join();
    }
    if (m_editor_pid.has_value())
        waitpid(m_editor_pid.value(), nullptr, WNOHANG);
}

ErrorOr<NonnullOwnPtr<MainWindow>> MainWindow::create(bool demo)
{
    auto main_window = TRY(adopt_nonnull_own_or_enomem(new (nothrow) MainWindow(demo)));
    TRY(main_window->build_ui());
    return main_window;
}

void MainWindow::show()
{
    m_window->show();
}

ErrorOr<void> MainWindow::build_ui()
{
    Vector<ByteString> icon_candidates;
    if (auto const* icon_path = getenv("HALCYON_SETUP_ICON"); icon_path && *icon_path)
        icon_candidates.append(ByteString { icon_path });
    icon_candidates.append("/usr/local/share/icons/hicolor/32x32/apps/halcyon-setup.png"sv);
    icon_candidates.append("/usr/share/icons/hicolor/32x32/apps/halcyon-setup.png"sv);
    for (auto const& icon_path : icon_candidates) {
        if (auto bitmap = Gfx::Bitmap::load_from_file(icon_path.view()); !bitmap.is_error()) {
            RefPtr<Gfx::Bitmap const> icon = bitmap.release_value();
            m_app_icon.set_bitmap_for_size(32, move(icon));
            break;
        }
    }

    m_window = GUI::Window::construct();
    m_window->set_title("Halcyon Linux Setup"sv);
    auto const* window_icon = m_app_icon.bitmap_for_size(32);
    if (!window_icon)
        window_icon = m_app_icon.bitmap_for_size(16);
    m_window->set_icon(window_icon);
    m_window->resize(820, 700);
    m_window->on_close_request = [this] {
        if (m_installing) {
            show_warning(*m_window, "The installation is still running. Use Cancel in the wizard to stop it."sv);
            return GUI::Window::CloseRequestDecision::StayOpen;
        }
        if (m_step != Step::Welcome && m_step != Step::Finished) {
            auto result = GUI::MessageBox::show(m_window, "Exit setup without finishing?"sv, "Halcyon Linux Setup"sv,
                GUI::MessageBox::Type::Question, GUI::MessageBox::InputType::YesNo);
            if (result != GUI::Dialog::ExecResult::Yes)
                return GUI::Window::CloseRequestDecision::StayOpen;
        }
        return GUI::Window::CloseRequestDecision::Close;
    };

    auto root_ref = m_window->set_main_widget<GUI::Widget>();
    auto& root = *root_ref;
    root.set_min_size(780, 680);
    root.set_fill_with_background_color(true);
    root.set_layout<GUI::VerticalBoxLayout>(GUI::Margins {}, 0);

    auto& header = root.add<WizardHeader>();
    if (window_icon)
        header.set_icon(RefPtr<Gfx::Bitmap const> { window_icon });
    header.set_subtitle("Setup is preparing your computer for Halcyon Linux."sv);

    auto& body = root.add<GUI::Widget>();
    body.set_layout<GUI::HorizontalBoxLayout>(GUI::Margins {}, 0);

    m_sidebar = body.add<WizardSidebar>();
    Vector<ByteString> steps;
    for (auto const* step : { "Welcome", "Disk", "Mount points", "Bootloader", "System", "Time zone", "Accounts", "Desktop", "Options", "Summary", "Install" })
        steps.append(ByteString { step });
    m_sidebar->set_steps(move(steps));

    m_stack = body.add<GUI::StackWidget>();

    build_pages();

    auto& footer = root.add<WizardFooter>();
    footer.set_layout<GUI::HorizontalBoxLayout>(GUI::Margins { 12, 10, 12, 10 }, 8);

    m_footer_hint = footer.add<GUI::Label>("Select Next to continue."_string);
    m_footer_hint->set_text_alignment(Gfx::TextAlignment::CenterLeft);
    m_footer_hint->set_foreground_role(Gfx::ColorRole::DisabledText);
    footer.add_spacer();

    m_back_button = footer.add<GUI::Button>("< Back"_string);
    m_back_button->set_fixed_width(88);
    m_back_button->on_click = [this](unsigned) {
        if (m_step == Step::Welcome)
            return;
        if (m_step == Step::Progress)
            return;
        if (m_step == Step::Finished) {
            m_window->close();
            return;
        }
        set_page(static_cast<Step>(static_cast<int>(m_step) - 1));
    };

    m_next_button = footer.add<GUI::Button>("Next >"_string);
    m_next_button->set_fixed_width(88);
    m_next_button->set_default(true);
    m_next_button->on_click = [this](unsigned) {
        if (m_step == Step::Finished) {
            m_window->close();
            return;
        }
        if (!validate_current_page())
            return;
        if (m_step == Step::Summary) {
            start_install();
            return;
        }
        set_page(static_cast<Step>(static_cast<int>(m_step) + 1));
    };

    m_cancel_button = footer.add<GUI::Button>("Cancel"_string);
    m_cancel_button->set_fixed_width(88);
    m_cancel_button->on_click = [this](unsigned) {
        if (m_installing) {
            cancel_install();
            return;
        }
        if (m_step == Step::Finished) {
            m_window->close();
            return;
        }

        auto result = GUI::MessageBox::show(m_window, "Exit setup without installing Halcyon Linux?"sv, "Halcyon Linux Setup"sv,
            GUI::MessageBox::Type::Question, GUI::MessageBox::InputType::YesNo);
        if (result == GUI::Dialog::ExecResult::Yes)
            m_window->close();
    };

    refresh_devices();
    set_page(Step::Welcome);
    return {};
}

void MainWindow::build_pages()
{
    // Welcome
    {
        auto& page = m_stack->add<GUI::Widget>();
        page.set_layout<GUI::VerticalBoxLayout>(GUI::Margins { 18, 14 }, 10);
        m_pages.append(&page);

        auto& title = page.add<GUI::Label>("Welcome to Halcyon Linux"_string);
        title.set_font_size(20);
        title.set_font_weight(700);
        title.set_fixed_height(30);
        title.set_text_alignment(Gfx::TextAlignment::CenterLeft);

        auto& intro = page.add<GUI::Label>(
            "This wizard installs Halcyon Linux on your computer. It will prepare the disk, copy the live system, "
            "and configure the time zone, user accounts and bootloader.\n\n"
            "Nothing is written to disk until you review the summary and press Install. Back up important data first: "
            "the installer can erase the entire selected disk."_string);
        intro.set_text_wrapping(Gfx::TextWrapping::Wrap);
        intro.set_text_alignment(Gfx::TextAlignment::CenterLeft);
        intro.set_fixed_height(72);

        auto& tip = page.add<GUI::Frame>();
        tip.set_frame_style(Gfx::FrameStyle::SunkenBox);
        tip.set_layout<GUI::VerticalBoxLayout>(GUI::Margins { 12 }, 6);
        tip.set_fixed_height(112);
        auto& tip_title = tip.add<GUI::Label>("Before you continue"_string);
        tip_title.set_font_weight(700);
        tip_title.set_text_alignment(Gfx::TextAlignment::CenterLeft);
        tip_title.set_fixed_height(20);
        auto& tip_text = tip.add<GUI::Label>(
            "The live session and the installation use the same Halcyon system. Keep the installation media "
            "connected until setup finishes, then remove it before restarting."_string);
        tip_text.set_text_wrapping(Gfx::TextWrapping::Wrap);
        tip_text.set_text_alignment(Gfx::TextAlignment::CenterLeft);
        tip_text.set_fixed_height(58);

        page.add_spacer();
    }

    build_disk_page();
    build_mounts_page();
    build_bootloader_page();
    build_system_page();
    build_timezone_page();
    build_accounts_page();
    build_desktop_page();
    build_options_page();
    build_summary_page();
    build_progress_page();
    build_finished_page();
}

void MainWindow::build_disk_page()
{
    auto& page = m_stack->add<GUI::Widget>();
    page.set_layout<GUI::VerticalBoxLayout>(GUI::Margins { 18, 14 }, 8);
    m_pages.append(&page);

    auto& title = page.add<GUI::Label>("Choose where to install Halcyon Linux"_string);
    title.set_font_size(18);
    title.set_font_weight(700);
    title.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    title.set_fixed_height(28);

    auto& intro = page.add<GUI::Label>(
        "The recommended option uses the whole device: a small FAT32 boot partition and a bcachefs root filesystem. "
        "Choose custom partitioning to keep existing partitions or select a different layout."_string);
    intro.set_text_wrapping(Gfx::TextWrapping::Wrap);
    intro.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    intro.set_fixed_height(58);

    m_entire_device_radio = page.add<GUI::RadioButton>("Use the entire device (recommended)"_string);
    m_entire_device_radio->set_checked(true);
    m_custom_layout_radio = page.add<GUI::RadioButton>("Use custom partitioning"_string);

    auto& device_row = page.add<GUI::Widget>();
    device_row.set_layout<GUI::HorizontalBoxLayout>(GUI::Margins {}, 8);
    device_row.set_fixed_height(30);
    auto& device_label = device_row.add<GUI::Label>("Install to:"_string);
    device_label.set_fixed_width(74);
    device_label.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    m_device_combo = device_row.add<GUI::ComboBox>();
    m_device_combo->set_only_allow_values_from_model(true);
    m_device_combo->set_max_visible_items(12);
    m_device_combo->on_change = [this](ByteString const&, GUI::ModelIndex const&) {
        if (!m_updating_device_combo)
            on_device_changed();
    };

    auto& summary_frame = page.add<GUI::Frame>();
    summary_frame.set_frame_style(Gfx::FrameStyle::SunkenBox);
    summary_frame.set_layout<GUI::VerticalBoxLayout>(GUI::Margins { 10, 7 }, 3);
    summary_frame.set_fixed_height(80);
    m_device_summary = summary_frame.add<GUI::Label>("No device selected."_string);
    m_device_summary->set_text_alignment(Gfx::TextAlignment::CenterLeft);
    m_device_summary->set_fixed_height(38);
    m_firmware_label = summary_frame.add<GUI::Label>(""_string);
    m_firmware_label->set_text_alignment(Gfx::TextAlignment::CenterLeft);
    m_firmware_label->set_foreground_role(Gfx::ColorRole::DisabledText);
    m_firmware_label->set_fixed_height(18);

    auto& plan_label = page.add<GUI::Label>("The installer will create:"_string);
    plan_label.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    plan_label.set_fixed_height(20);

    m_plan_preview = page.add<GUI::TextEditor>();
    m_plan_preview->set_mode(GUI::TextEditor::Mode::ReadOnly);
    m_plan_preview->set_fixed_height(94);
    m_plan_preview->set_text(""_string);

    m_erase_confirm = page.add<GUI::CheckBox>("I understand that all data on the selected device will be erased."_string);
    m_erase_confirm->set_checked(false);

    auto& buttons = page.add<GUI::Widget>();
    buttons.set_layout<GUI::HorizontalBoxLayout>(GUI::Margins {}, 8);
    buttons.set_fixed_height(34);
    m_partition_editor_button = buttons.add<GUI::Button>("Partition Editor..."_string);
    m_partition_editor_button->set_fixed_width(144);
    m_partition_editor_button->on_click = [this](unsigned) {
        launch_partition_editor();
    };
    m_refresh_devices_button = buttons.add<GUI::Button>("Refresh disks"_string);
    m_refresh_devices_button->set_fixed_width(112);
    m_refresh_devices_button->on_click = [this](unsigned) {
        refresh_devices();
    };
    buttons.add_spacer();

    m_entire_device_radio->on_checked = [this](bool checked) {
        if (!checked)
            return;
        m_erase_confirm->set_visible(true);
        update_disk_preview();
        update_mount_table();
        update_buttons();
    };
    m_custom_layout_radio->on_checked = [this](bool checked) {
        if (!checked)
            return;
        m_erase_confirm->set_visible(false);
        update_disk_preview();
        update_mount_table();
        update_buttons();
    };
}

void MainWindow::build_mounts_page()
{
    auto& page = m_stack->add<GUI::Widget>();
    page.set_layout<GUI::VerticalBoxLayout>(GUI::Margins { 18, 14 }, 8);
    m_pages.append(&page);

    auto& title = page.add<GUI::Label>("Assign mount points"_string);
    title.set_font_size(18);
    title.set_font_weight(700);
    title.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    title.set_fixed_height(28);

    m_mount_hint = page.add<GUI::Label>(""_string);
    m_mount_hint->set_text_wrapping(Gfx::TextWrapping::Wrap);
    m_mount_hint->set_text_alignment(Gfx::TextAlignment::CenterLeft);
    m_mount_hint->set_fixed_height(44);

    m_mount_model = MountTableModel::create();
    m_mount_table = page.add<GUI::TableView>();
    m_mount_table->set_model(*m_mount_model);
    m_mount_table->set_column_width(MountTableModel::Device, 140);
    m_mount_table->set_column_width(MountTableModel::Size, 80);
    m_mount_table->set_column_width(MountTableModel::Filesystem, 74);
    m_mount_table->set_column_width(MountTableModel::Label, 92);
    m_mount_table->set_column_width(MountTableModel::MountPoint, 112);
    m_mount_table->set_column_width(MountTableModel::Format, 50);
    m_mount_table->set_selection_behavior(GUI::AbstractView::SelectionBehavior::SelectRows);
    m_mount_table->on_selection_change = [this] {
        update_buttons();
    };

    auto& buttons = page.add<GUI::Widget>();
    buttons.set_layout<GUI::HorizontalBoxLayout>(GUI::Margins {}, 8);
    buttons.set_fixed_height(34);
    m_set_mount_button = buttons.add<GUI::Button>("Set Mount Point..."_string);
    m_set_mount_button->set_fixed_width(132);
    m_set_mount_button->on_click = [this](unsigned) {
        set_selected_mountpoint();
    };
    m_format_button = buttons.add<GUI::Button>("Format..."_string);
    m_format_button->set_fixed_width(88);
    m_format_button->on_click = [this](unsigned) {
        format_selected_partition();
    };
    m_mount_editor_button = buttons.add<GUI::Button>("Partition Editor..."_string);
    m_mount_editor_button->set_fixed_width(144);
    m_mount_editor_button->on_click = [this](unsigned) {
        launch_partition_editor();
    };
    m_mount_refresh_button = buttons.add<GUI::Button>("Refresh"_string);
    m_mount_refresh_button->set_fixed_width(84);
    m_mount_refresh_button->on_click = [this](unsigned) {
        refresh_devices();
    };
    buttons.add_spacer();
}

void MainWindow::build_bootloader_page()
{
    auto& page = m_stack->add<GUI::Widget>();
    page.set_layout<GUI::VerticalBoxLayout>(GUI::Margins { 18, 14 }, 9);
    m_pages.append(&page);

    auto& title = page.add<GUI::Label>("Choose a bootloader"_string);
    title.set_font_size(18);
    title.set_font_weight(700);
    title.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    title.set_fixed_height(28);

    auto& intro = page.add<GUI::Label>(
        "The bootloader starts Halcyon Linux when the computer powers on. Limine is the default on Halcyon "
        "installation media; GRUB is also available and follows the Chimera handbook."_string);
    intro.set_text_wrapping(Gfx::TextWrapping::Wrap);
    intro.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    intro.set_fixed_height(58);

    m_limine_radio = page.add<GUI::RadioButton>("Limine (recommended for Halcyon)"_string);
    m_grub_radio = page.add<GUI::RadioButton>("GRUB (Chimera Linux default)"_string);
    if (m_demo || command_exists("limine"sv)) {
        m_limine_radio->set_checked(true);
    } else {
        m_limine_radio->set_enabled(false);
        m_grub_radio->set_checked(true);
    }

    auto& note = page.add<GUI::Label>(
        "Limine can boot from a FAT32 /boot partition. If you selected a custom layout without a FAT32 /boot, "
        "use GRUB instead. The installer writes the bootloader configuration after generating the initial ramdisk."_string);
    note.set_text_wrapping(Gfx::TextWrapping::Wrap);
    note.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    note.set_foreground_role(Gfx::ColorRole::DisabledText);
    note.set_fixed_height(58);

    page.add_spacer();
}

void MainWindow::build_system_page()
{
    auto& page = m_stack->add<GUI::Widget>();
    page.set_layout<GUI::VerticalBoxLayout>(GUI::Margins { 18, 14 }, 9);
    m_pages.append(&page);

    auto& title = page.add<GUI::Label>("System settings"_string);
    title.set_font_size(18);
    title.set_font_weight(700);
    title.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    title.set_fixed_height(28);

    auto& hostname_row = page.add<GUI::Widget>();
    hostname_row.set_layout<GUI::HorizontalBoxLayout>(GUI::Margins {}, 8);
    hostname_row.set_fixed_height(30);
    auto& hostname_label = hostname_row.add<GUI::Label>("Computer name:"_string);
    hostname_label.set_fixed_width(112);
    hostname_label.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    m_hostname_input = hostname_row.add<GUI::TextBox>();
    m_hostname_input->set_text("halcyon"_string);
    auto& hostname_hint = hostname_row.add<GUI::Label>("(hostname)"_string);
    hostname_hint.set_foreground_role(Gfx::ColorRole::DisabledText);

    auto& keyboard_row = page.add<GUI::Widget>();
    keyboard_row.set_layout<GUI::HorizontalBoxLayout>(GUI::Margins {}, 8);
    keyboard_row.set_fixed_height(30);
    auto& keyboard_label = keyboard_row.add<GUI::Label>("Keyboard:"_string);
    keyboard_label.set_fixed_width(112);
    keyboard_label.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    m_keyboard_combo = keyboard_row.add<GUI::ComboBox>();
    m_keyboard_combo->set_only_allow_values_from_model(true);
    // ItemListModel keeps a reference to this container, so it must outlive
    // the combo box. A local vector here left the model dangling and crashed
    // the application when the drop-down opened.
    m_keyboard_layouts = {
        "us"sv, "gb"sv, "de"sv, "fr"sv, "es"sv, "it"sv, "pt"sv, "nl"sv, "se"sv, "no"sv,
        "dk"sv, "fi"sv, "pl"sv, "cz"sv, "ru"sv, "tr"sv, "br"sv, "jp"sv,
    };
    m_keyboard_combo->set_model(*GUI::ItemListModel<ByteString>::create(m_keyboard_layouts));
    m_keyboard_combo->set_text("us"sv);

    auto& rtc_label = page.add<GUI::Label>("Hardware clock:"_string);
    rtc_label.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    rtc_label.set_fixed_height(20);
    m_rtc_utc_radio = page.add<GUI::RadioButton>("Store the hardware clock in UTC (recommended)"_string);
    m_rtc_utc_radio->set_checked(true);
    m_rtc_localtime_radio = page.add<GUI::RadioButton>("Store the hardware clock in local time (for dual-boot with Windows)"_string);

    auto& note = page.add<GUI::Label>(
        "The hostname is written to /etc/hostname. The keyboard layout is written to /etc/default/keyboard for "
        "the console and graphical sessions."_string);
    note.set_text_wrapping(Gfx::TextWrapping::Wrap);
    note.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    note.set_foreground_role(Gfx::ColorRole::DisabledText);
    note.set_fixed_height(52);

    page.add_spacer();
}

void MainWindow::build_timezone_page()
{
    auto& page = m_stack->add<GUI::Widget>();
    page.set_layout<GUI::VerticalBoxLayout>(GUI::Margins { 18, 14 }, 8);
    m_pages.append(&page);

    auto& title = page.add<GUI::Label>("Choose your time zone"_string);
    title.set_font_size(18);
    title.set_font_weight(700);
    title.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    title.set_fixed_height(28);

    auto& intro = page.add<GUI::Label>(
        "Click a point on the map to select the nearest time zone, or choose a zone from the list. "
        "The time zone is installed as /etc/localtime."_string);
    intro.set_text_wrapping(Gfx::TextWrapping::Wrap);
    intro.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    intro.set_fixed_height(58);

    auto& zone_row = page.add<GUI::Widget>();
    zone_row.set_layout<GUI::HorizontalBoxLayout>(GUI::Margins {}, 8);
    zone_row.set_fixed_height(30);
    auto& zone_label = zone_row.add<GUI::Label>("Time zone:"_string);
    zone_label.set_fixed_width(74);
    zone_label.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    m_timezone_combo = zone_row.add<GUI::ComboBox>();
    m_timezone_combo->set_only_allow_values_from_model(true);
    m_timezone_combo->set_max_visible_items(16);

    m_timezone_label = page.add<GUI::Label>("UTC"_string);
    m_timezone_label->set_text_alignment(Gfx::TextAlignment::CenterLeft);
    m_timezone_label->set_foreground_role(Gfx::ColorRole::DisabledText);
    m_timezone_label->set_fixed_height(20);

    m_world_map = page.add<WorldMap>();
    m_world_map->set_min_height(220);
    m_world_map->on_zone_selected = [this](ByteString const& zone) {
        set_timezone(zone);
    };

    m_timezone_combo->on_change = [this](ByteString const& text, GUI::ModelIndex const&) {
        if (m_updating_timezone)
            return;
        set_timezone(text);
    };

    m_timezone_names.append("UTC"sv);
    Vector<WorldMap::ZonePoint> points;
    for (auto const& zone : TimeZone::all_time_zones()) {
        if (zone.is_link == TimeZone::IsLink::Yes)
            continue;
        auto location = TimeZone::get_time_zone_location(zone.name);
        if (!location.has_value())
            continue;
        m_timezone_names.append(ByteString { zone.name });
        WorldMap::ZonePoint point;
        point.name = ByteString { zone.name };
        point.latitude = location->latitude.decimal_coordinate();
        point.longitude = location->longitude.decimal_coordinate();
        points.append(move(point));
    }

    auto system_zone = TimeZone::system_time_zone();
    if (!system_zone.is_empty()) {
        if (auto canonical = TimeZone::canonicalize_time_zone(system_zone); canonical.has_value())
            m_timezone = ByteString { *canonical };
        else
            m_timezone = ByteString { system_zone };
    }
    if (m_timezone.is_empty())
        m_timezone = "UTC"sv;

    m_world_map->set_zones(move(points));
    m_timezone_combo->set_model(*GUI::ItemListModel<ByteString>::create(m_timezone_names));
    set_timezone(m_timezone);
}

void MainWindow::build_accounts_page()
{
    auto& page = m_stack->add<GUI::Widget>();
    page.set_layout<GUI::VerticalBoxLayout>(GUI::Margins { 18, 14 }, 8);
    m_pages.append(&page);

    auto& title = page.add<GUI::Label>("Passwords and user accounts"_string);
    title.set_font_size(18);
    title.set_font_weight(700);
    title.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    title.set_fixed_height(28);

    auto& root_box = page.add<GUI::GroupBox>("Root password"sv);
    root_box.set_layout<GUI::VerticalBoxLayout>(GUI::Margins { 10, 14, 10, 10 }, 7);
    root_box.set_fixed_height(156);

    auto& root_note = root_box.add<GUI::Label>(
        "The root account is the administrator. Set a password now; you will need it to repair the system."_string);
    root_note.set_text_wrapping(Gfx::TextWrapping::Wrap);
    root_note.set_text_alignment(Gfx::TextAlignment::CenterLeft);

    auto& root_password_row = root_box.add<GUI::Widget>();
    root_password_row.set_layout<GUI::HorizontalBoxLayout>(GUI::Margins {}, 8);
    auto& root_password_label = root_password_row.add<GUI::Label>("Password:"_string);
    root_password_label.set_fixed_width(88);
    m_root_password = root_password_row.add<GUI::PasswordBox>();

    auto& root_confirm_row = root_box.add<GUI::Widget>();
    root_confirm_row.set_layout<GUI::HorizontalBoxLayout>(GUI::Margins {}, 8);
    auto& root_confirm_label = root_confirm_row.add<GUI::Label>("Confirm:"_string);
    root_confirm_label.set_fixed_width(88);
    m_root_confirm = root_confirm_row.add<GUI::PasswordBox>();

    auto& user_box = page.add<GUI::GroupBox>("User accounts"sv);
    user_box.set_layout<GUI::VerticalBoxLayout>(GUI::Margins { 10, 14, 10, 10 }, 6);
    user_box.set_min_height(190);

    m_user_model = UserTableModel::create();
    m_user_table = user_box.add<GUI::TableView>();
    m_user_table->set_model(*m_user_model);
    m_user_table->set_column_width(UserTableModel::Username, 150);
    m_user_table->set_column_width(UserTableModel::FullName, 200);
    m_user_table->set_column_width(UserTableModel::Groups, 220);
    m_user_table->set_selection_behavior(GUI::AbstractView::SelectionBehavior::SelectRows);
    m_user_table->on_selection_change = [this] {
        update_buttons();
    };

    auto& user_buttons = user_box.add<GUI::Widget>();
    user_buttons.set_layout<GUI::HorizontalBoxLayout>(GUI::Margins {}, 8);
    user_buttons.set_fixed_height(34);
    auto& add = user_buttons.add<GUI::Button>("Add User..."_string);
    add.set_fixed_width(104);
    add.on_click = [this](unsigned) {
        add_user();
    };
    auto& remove = user_buttons.add<GUI::Button>("Remove"_string);
    remove.set_fixed_width(88);
    remove.on_click = [this](unsigned) {
        remove_user();
    };
    user_buttons.add_spacer();

    m_user_hint = user_box.add<GUI::Label>(
        "Create at least one normal user account. It is added to the wheel group by default so it can administer the system."_string);
    m_user_hint->set_text_wrapping(Gfx::TextWrapping::Wrap);
    m_user_hint->set_text_alignment(Gfx::TextAlignment::CenterLeft);
    m_user_hint->set_foreground_role(Gfx::ColorRole::DisabledText);
    m_user_hint->set_fixed_height(34);

    page.add_spacer();
}

void MainWindow::build_desktop_page()
{
    auto& page = m_stack->add<GUI::Widget>();
    page.set_layout<GUI::VerticalBoxLayout>(GUI::Margins { 18, 14 }, 9);
    m_pages.append(&page);

    auto& title = page.add<GUI::Label>("Desktop environment"_string);
    title.set_font_size(18);
    title.set_font_weight(700);
    title.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    title.set_fixed_height(28);

    auto& intro = page.add<GUI::Label>(
        "Choose whether the installed system should start the Serenity Desktop Environment. The base "
        "system, the GNU userland, bcachefs tools and the kernel modules are installed either way."_string);
    intro.set_text_wrapping(Gfx::TextWrapping::Wrap);
    intro.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    intro.set_fixed_height(58);

    m_install_desktop_checkbox = page.add<GUI::CheckBox>("Install the Serenity Desktop Environment (SDE)"_string);
    m_install_desktop_checkbox->set_checked(true);

    auto& note = page.add<GUI::Label>(
        "Clearing this option removes the SDE session, display manager and SDE browser from the target. "
        "The system will boot to a text console; you can install a desktop later with apk."_string);
    note.set_text_wrapping(Gfx::TextWrapping::Wrap);
    note.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    note.set_foreground_role(Gfx::ColorRole::DisabledText);
    note.set_fixed_height(52);

    page.add_spacer();
}

void MainWindow::build_options_page()
{
    auto& page = m_stack->add<GUI::Widget>();
    page.set_layout<GUI::VerticalBoxLayout>(GUI::Margins { 18, 14 }, 9);
    m_pages.append(&page);

    auto& title = page.add<GUI::Label>("Post-installation options"_string);
    title.set_font_size(18);
    title.set_font_weight(700);
    title.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    title.set_fixed_height(28);

    auto& intro = page.add<GUI::Label>(
        "These settings follow the post-installation tasks in the Chimera Linux handbook. They are applied "
        "inside the installed system before the bootloader is configured."_string);
    intro.set_text_wrapping(Gfx::TextWrapping::Wrap);
    intro.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    intro.set_fixed_height(58);

    m_remove_base_live_checkbox = page.add<GUI::CheckBox>("Remove the base-live package after installation"_string);
    m_remove_base_live_checkbox->set_checked(true);
    m_update_system_checkbox = page.add<GUI::CheckBox>("Update the installed system from the configured repositories"_string);
    m_update_system_checkbox->set_checked(true);
    m_enable_syslog_checkbox = page.add<GUI::CheckBox>("Enable the syslog-ng service"_string);
    m_enable_syslog_checkbox->set_checked(true);
    m_enable_networkmanager_checkbox = page.add<GUI::CheckBox>("Enable NetworkManager if it is installed"_string);
    m_enable_networkmanager_checkbox->set_checked(true);
    m_enable_ntp_checkbox = page.add<GUI::CheckBox>("Enable NTP time synchronization (chrony)"_string);
    m_enable_ntp_checkbox->set_checked(true);

    auto& note = page.add<GUI::Label>(
        "Updating from the network requires a working connection. Local installation from the live media does not "
        "need a network for the base system."_string);
    note.set_text_wrapping(Gfx::TextWrapping::Wrap);
    note.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    note.set_foreground_role(Gfx::ColorRole::DisabledText);
    note.set_fixed_height(44);

    page.add_spacer();
}

void MainWindow::build_summary_page()
{
    auto& page = m_stack->add<GUI::Widget>();
    page.set_layout<GUI::VerticalBoxLayout>(GUI::Margins { 18, 14 }, 8);
    m_pages.append(&page);

    auto& title = page.add<GUI::Label>("Ready to install"_string);
    title.set_font_size(18);
    title.set_font_weight(700);
    title.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    title.set_fixed_height(28);

    auto& intro = page.add<GUI::Label>(
        "Review the installation summary. Press Install to write the changes. The installer will partition and "
        "format the selected disk, copy the live system, and configure the installed system."_string);
    intro.set_text_wrapping(Gfx::TextWrapping::Wrap);
    intro.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    intro.set_fixed_height(58);

    m_summary_view = page.add<GUI::TextEditor>();
    m_summary_view->set_mode(GUI::TextEditor::Mode::ReadOnly);
    m_summary_view->set_text(""_string);

    m_final_confirm = page.add<GUI::CheckBox>("I have reviewed the summary and want to install Halcyon Linux."_string);
    m_final_confirm->set_checked(false);

    page.add_spacer();
}

void MainWindow::build_progress_page()
{
    auto& page = m_stack->add<GUI::Widget>();
    page.set_layout<GUI::VerticalBoxLayout>(GUI::Margins { 18, 14 }, 9);
    m_pages.append(&page);

    auto& title = page.add<GUI::Label>("Installing Halcyon Linux"_string);
    title.set_font_size(18);
    title.set_font_weight(700);
    title.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    title.set_fixed_height(28);

    m_progress_status = page.add<GUI::Label>("Preparing..."_string);
    m_progress_status->set_text_alignment(Gfx::TextAlignment::CenterLeft);
    m_progress_status->set_fixed_height(20);

    m_progress_bar = page.add<GUI::HorizontalProgressbar>();
    m_progress_bar->set_range(0, 100);
    m_progress_bar->set_value(0);
    m_progress_bar->set_fixed_height(24);

    auto& log_label = page.add<GUI::Label>("Installation log:"_string);
    log_label.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    log_label.set_fixed_height(20);

    m_log_view = page.add<GUI::TextEditor>();
    m_log_view->set_mode(GUI::TextEditor::Mode::ReadOnly);
    m_log_view->set_text(""_string);
}

void MainWindow::build_finished_page()
{
    auto& page = m_stack->add<GUI::Widget>();
    page.set_layout<GUI::VerticalBoxLayout>(GUI::Margins { 18, 14 }, 10);
    m_pages.append(&page);

    m_finished_title = page.add<GUI::Label>("Setup complete"_string);
    m_finished_title->set_font_size(20);
    m_finished_title->set_font_weight(700);
    m_finished_title->set_text_alignment(Gfx::TextAlignment::CenterLeft);
    m_finished_title->set_fixed_height(30);

    m_finished_text = page.add<GUI::Label>(""_string);
    m_finished_text->set_text_wrapping(Gfx::TextWrapping::Wrap);
    m_finished_text->set_text_alignment(Gfx::TextAlignment::CenterLeft);
    m_finished_text->set_fixed_height(120);

    auto& note = page.add<GUI::Label>(
        "The installation log is available above on the previous page if you need to review it. "
        "Support and documentation are available at chimera-linux.org and in the Halcyon Linux project."_string);
    note.set_text_wrapping(Gfx::TextWrapping::Wrap);
    note.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    note.set_foreground_role(Gfx::ColorRole::DisabledText);
    note.set_fixed_height(52);

    page.add_spacer();
}

DiskInfo const* MainWindow::selected_disk() const
{
    auto index = m_device_combo ? m_device_combo->selected_index() : 0;
    if (index >= m_device_paths.size())
        return nullptr;
    auto const& path = m_device_paths[index];
    for (auto const& disk : m_disks) {
        if (disk.path == path)
            return &disk;
    }
    return nullptr;
}

void MainWindow::refresh_devices()
{
    auto selected = selected_disk();
    auto selected_path = selected ? selected->path : ByteString {};

    DeviceProbe probe;
    if (m_demo) {
        probe.disks = demo_devices();
        probe.uefi = true;
    } else {
        probe = probe_devices();
    }

    m_disks = move(probe.disks);
    m_uefi = probe.uefi;
    m_probe_error = probe.error;

    m_device_paths.clear();
    // The model keeps a reference to the container, so the labels live on the
    // window rather than in this function.
    m_device_labels.clear();
    for (auto const& disk : m_disks) {
        m_device_paths.append(disk.path);
        auto model = disk.model.is_empty() ? "Unknown device"sv : disk.model.view();
        m_device_labels.append(ByteString::formatted("{}  -  {}  -  {}", disk.path, size_string(disk.size_bytes), model));
    }

    m_updating_device_combo = true;
    if (m_device_combo) {
        m_device_combo->set_model(*GUI::ItemListModel<ByteString>::create(m_device_labels));
        size_t selection = 0;
        for (size_t index = 0; index < m_device_paths.size(); ++index) {
            if (m_device_paths[index] == selected_path) {
                selection = index;
                break;
            }
        }
        if (!m_device_paths.is_empty())
            m_device_combo->set_selected_index(selection);
    }
    m_updating_device_combo = false;

    if (!m_probe_error.is_empty()) {
        m_device_summary->set_text(MUST(String::formatted("Could not read disks: {}", m_probe_error)));
    }

    on_device_changed();
}

void MainWindow::on_device_changed()
{
    m_erase_confirm->set_checked(false);
    update_disk_preview();
    update_mount_table();
    update_buttons();
}

void MainWindow::update_disk_preview()
{
    auto const* disk = selected_disk();
    if (!disk) {
        m_device_summary->set_text("No disk selected. Connect a disk and press Refresh disks."_string);
        m_firmware_label->set_text(to_string(m_uefi ? "Firmware: UEFI"sv : "Firmware: legacy BIOS"sv));
        m_plan_preview->set_text(""_string);
        return;
    }

    m_device_summary->set_text(MUST(String::formatted("{}  -  {}\n{} partitions, {}", disk->path, size_string(disk->size_bytes),
        disk->partitions.size(), disk->table_type.is_empty() ? ByteString { "no partition table" } : disk->table_type)));
    m_firmware_label->set_text(to_string(m_uefi
            ? "Firmware: UEFI (the installer creates a 2 GiB FAT32 EFI system partition)"sv
            : "Firmware: legacy BIOS (a small BIOS boot partition will be created)"sv));

    if (m_custom_layout_radio && m_custom_layout_radio->is_checked()) {
        m_plan_preview->set_text(ByteString::formatted(
            "Custom layout on {}\n\n{} partitions are available. Use the mount-point table to assign the root "
            "filesystem and other mount points. Use Partition Editor to create or change partitions.",
            disk->path, disk->partitions.size()));
        return;
    }

    m_plan = whole_device_plan(*disk, m_uefi);
    if (!m_plan.error.is_empty()) {
        m_plan_preview->set_text(m_plan.error.view());
        return;
    }
    m_plan_preview->set_text(describe_plan(m_plan.partitions, m_uefi));
}

void MainWindow::update_mount_table()
{
    if (!m_mount_model)
        return;

    auto const* disk = selected_disk();
    if (!disk) {
        m_mount_model->set_rows({});
        m_mount_hint->set_text("No disk selected."_string);
        update_buttons();
        return;
    }

    bool custom = m_custom_layout_radio && m_custom_layout_radio->is_checked();
    Vector<MountRow> rows;

    if (!custom) {
        if (m_plan.partitions.is_empty())
            m_plan = whole_device_plan(*disk, m_uefi);
        for (auto const& partition : m_plan.partitions) {
            if (partition.bios_boot)
                continue;
            MountRow row;
            row.partition = partition.path;
            row.size_bytes = partition.size_bytes;
            row.fs_type = partition.fs_type;
            row.label = partition.label;
            row.mountpoint = partition.mountpoint;
            row.format = true;
            rows.append(move(row));
        }
        m_mount_hint->set_text("The default layout is fixed. Switch to custom partitioning to assign mount points yourself."_string);
    } else {
        auto existing = m_mount_model->rows();
        bool has_root = false;
        bool has_boot = false;
        for (auto const& row : existing) {
            if (row.mountpoint == "/"sv)
                has_root = true;
            if (row.mountpoint == "/boot"sv || row.mountpoint == "/boot/efi"sv)
                has_boot = true;
        }

        for (auto const& partition : disk->partitions) {
            MountRow row;
            row.partition = partition.path;
            row.size_bytes = partition.size_bytes;
            row.fs_type = partition.fs_type;
            row.label = partition.label;
            row.format = false;

            for (auto const& old : existing) {
                if (old.partition != partition.path)
                    continue;
                row.mountpoint = old.mountpoint;
                row.format = old.format;
                break;
            }

            if (row.mountpoint.is_empty()) {
                if (filesystem_is_fat(partition.fs_type) && !has_boot) {
                    row.mountpoint = "/boot"sv;
                    has_boot = true;
                } else if (partition.fs_type == "swap"sv) {
                    row.mountpoint = "swap"sv;
                } else if (!has_root && (partition.fs_type == "bcachefs"sv || partition.fs_type == "ext4"sv || partition.fs_type == "btrfs"sv || partition.fs_type == "xfs"sv)) {
                    row.mountpoint = "/"sv;
                    has_root = true;
                }
            }

            rows.append(move(row));
        }
        m_mount_hint->set_text(to_string(rows.is_empty()
                ? "This disk has no partitions. Use Partition Editor to create a partition table and partitions."sv
                : "Assign / to the root filesystem and /boot to a FAT32 or ext4 boot partition. Use swap for swap partitions. Set Format when the installer should create a new filesystem."sv));
    }

    m_mount_model->set_rows(move(rows));
    update_buttons();
}

int MainWindow::selected_mount_row() const
{
    if (!m_mount_table || m_mount_table->selection().is_empty())
        return -1;
    return m_mount_table->selection().first().row();
}

int MainWindow::selected_user_row() const
{
    if (!m_user_table || m_user_table->selection().is_empty())
        return -1;
    return m_user_table->selection().first().row();
}

void MainWindow::update_user_model()
{
    Vector<ByteString> usernames;
    Vector<ByteString> full_names;
    Vector<ByteString> groups;
    for (auto const& user : m_users) {
        usernames.append(user.username);
        full_names.append(user.full_name);
        StringBuilder builder;
        for (size_t i = 0; i < user.groups.size(); ++i) {
            if (i > 0)
                builder.append(", "sv);
            builder.append(user.groups[i]);
        }
        groups.append(builder.to_byte_string());
    }
    m_user_model->set_users(move(usernames), move(full_names), move(groups));
}

void MainWindow::update_timezone_display()
{
    if (!m_timezone_label)
        return;

    m_timezone_label->set_text(MUST(String::formatted("Selected time zone: {}", m_timezone)));
}

void MainWindow::set_timezone(ByteString const& zone)
{
    if (zone.is_empty())
        return;

    m_timezone = zone;
    m_updating_timezone = true;
    if (m_timezone_combo)
        m_timezone_combo->set_text(zone, GUI::AllowCallback::No);
    if (m_world_map)
        m_world_map->set_timezone(zone);
    update_timezone_display();
    m_updating_timezone = false;
}

void MainWindow::update_summary()
{
    if (!m_summary_view)
        return;

    auto config = build_config();
    StringBuilder builder;
    builder.appendff("Target device: {}\n", config.target_device);
    builder.appendff("Layout: {}\n", config.layout == LayoutMode::EntireDevice ? "Use the entire device"sv : "Custom partitioning"sv);
    builder.appendff("Firmware: {}\n\n", config.uefi ? "UEFI"sv : "Legacy BIOS"sv);

    builder.append("Partitions and mount points:\n"sv);
    for (auto const& mount : config.mounts) {
        if (mount.mountpoint.is_empty())
            continue;
        builder.appendff("  {}  {}  {}  {}{}\n", mount.partition, filesystem_display_name(mount.fs_type),
            mount.mountpoint == "swap"sv ? ByteString { "swap" } : mount.mountpoint, mount.format ? " (format)"sv : ""sv,
            mount.label.is_empty() ? ByteString {} : mount.label);
    }

    builder.appendff("\nHostname: {}\n", config.hostname);
    builder.appendff("Time zone: {}\n", config.timezone);
    builder.appendff("Keyboard: {}\n", keyboard_description(config.keyboard_layout));
    builder.appendff("Hardware clock: {}\n", config.hardware_clock_localtime ? "local time"sv : "UTC"sv);
    builder.appendff("Bootloader: {}\n", bootloader_name(config.bootloader));
    builder.appendff("Desktop environment: {}\n", config.install_desktop ? "Serenity Desktop Environment"sv : "none"sv);
    builder.appendff("Root password: (set)\n");
    if (config.users.is_empty()) {
        builder.append("Users: (none - root only)\n"sv);
    } else {
        builder.append("Users:\n"sv);
        for (auto const& user : config.users)
            builder.appendff("  {} ({})\n", user.username, user.full_name);
    }

    builder.append("\nOptions:\n"sv);
    builder.appendff("  Remove base-live: {}\n", config.remove_base_live ? "yes"sv : "no"sv);
    builder.appendff("  Update installed system: {}\n", config.update_system ? "yes"sv : "no"sv);
    builder.appendff("  Enable syslog-ng: {}\n", config.enable_syslog ? "yes"sv : "no"sv);
    builder.appendff("  Enable NetworkManager: {}\n", config.enable_networkmanager ? "yes"sv : "no"sv);
    builder.appendff("  Enable NTP: {}\n", config.enable_ntp ? "yes"sv : "no"sv);

    auto summary = builder.to_byte_string();
    m_summary_view->set_text(summary.view());
}

InstallConfig MainWindow::build_config() const
{
    InstallConfig config;
    config.uefi = m_uefi;
    config.layout = (m_custom_layout_radio && m_custom_layout_radio->is_checked()) ? LayoutMode::Custom : LayoutMode::EntireDevice;

    auto const* disk = selected_disk();
    if (disk)
        config.target_device = disk->path;

    if (config.layout == LayoutMode::EntireDevice && disk) {
        auto plan = whole_device_plan(*disk, m_uefi);
        config.mounts = mounts_from_plan(plan.partitions);
    } else if (m_mount_model) {
        for (auto const& row : m_mount_model->rows()) {
            if (row.mountpoint.is_empty())
                continue;
            MountAssignment mount;
            mount.partition = row.partition;
            mount.mountpoint = row.mountpoint;
            mount.fs_type = row.fs_type;
            mount.label = row.label;
            mount.format = row.format;
            config.mounts.append(move(mount));
        }
    }

    config.hostname = m_hostname_input ? m_hostname_input->text() : ByteString { "halcyon" };
    config.timezone = m_timezone;
    config.keyboard_layout = m_keyboard_combo ? m_keyboard_combo->text() : ByteString { "us" };
    config.hardware_clock_localtime = m_rtc_localtime_radio && m_rtc_localtime_radio->is_checked();
    config.bootloader = (m_grub_radio && m_grub_radio->is_checked()) ? Bootloader::Grub : Bootloader::Limine;

    config.install_desktop = !m_install_desktop_checkbox || m_install_desktop_checkbox->is_checked();
    config.remove_base_live = !m_remove_base_live_checkbox || m_remove_base_live_checkbox->is_checked();
    config.update_system = !m_update_system_checkbox || m_update_system_checkbox->is_checked();
    config.enable_syslog = !m_enable_syslog_checkbox || m_enable_syslog_checkbox->is_checked();
    config.enable_networkmanager = !m_enable_networkmanager_checkbox || m_enable_networkmanager_checkbox->is_checked();
    config.enable_ntp = !m_enable_ntp_checkbox || m_enable_ntp_checkbox->is_checked();

    if (m_root_password)
        config.root_password = m_root_password->text();
    config.users = m_users;
    return config;
}

bool MainWindow::validate_current_page()
{
    switch (m_step) {
    case Step::Welcome:
        return true;

    case Step::Disk: {
        auto const* disk = selected_disk();
        if (!disk) {
            show_warning(*m_window, "No installation disk is selected. Press Refresh disks and choose a device."sv);
            return false;
        }
        if (m_custom_layout_radio->is_checked()) {
            if (disk->partitions.is_empty()) {
                show_warning(*m_window, "This disk has no partitions. Use Partition Editor to create a partition table and partitions."sv);
                return false;
            }
            return true;
        }
        if (!m_erase_confirm->is_checked()) {
            show_warning(*m_window, "Confirm that you understand all data on the selected device will be erased."sv);
            return false;
        }
        return true;
    }

    case Step::Mounts: {
        auto config = build_config();
        // Only storage is entered on this page. Hostname, time zone and
        // accounts are later steps; validating them here made Next fail with
        // "Enter a root password" before the Accounts page could be reached.
        auto error = validate_storage_config(config, m_disks);
        if (!error.is_empty()) {
            show_warning(*m_window, error.view());
            return false;
        }
        return true;
    }

    case Step::Bootloader:
        return true;

    case Step::System: {
        if (!m_hostname_input || !hostname_is_valid(m_hostname_input->text())) {
            show_warning(*m_window, "Enter a valid hostname (letters, digits, dots and hyphens; no leading or trailing hyphen)."sv);
            return false;
        }
        return true;
    }

    case Step::Timezone:
        if (m_timezone.is_empty()) {
            show_warning(*m_window, "Choose a time zone."sv);
            return false;
        }
        return true;

    case Step::Accounts: {
        if (!m_root_password || m_root_password->text().is_empty()) {
            show_warning(*m_window, "Enter a root password. The system cannot be administered without one."sv);
            return false;
        }
        if (!m_root_confirm || m_root_password->text() != m_root_confirm->text()) {
            show_warning(*m_window, "The root passwords do not match."sv);
            return false;
        }
        for (auto const& user : m_users) {
            if (user.username.is_empty() || user.password.is_empty()) {
                show_warning(*m_window, "Every user account needs a user name and password."sv);
                return false;
            }
        }
        return true;
    }

    case Step::Desktop:
        return true;

    case Step::Options:
        return true;

    case Step::Summary:
        if (!m_final_confirm || !m_final_confirm->is_checked()) {
            show_warning(*m_window, "Review the summary and check the confirmation box before installing."sv);
            return false;
        }
        return validate_all();

    case Step::Progress:
    case Step::Finished:
    case Step::__Count:
        return true;
    }

    return true;
}

bool MainWindow::validate_all()
{
    auto config = build_config();
    auto error = validate_config(config, m_disks);
    if (error.is_empty())
        return true;
    show_warning(*m_window, error.view());
    return false;
}

void MainWindow::set_page(Step step)
{
    m_step = step;
    if (m_stack && static_cast<size_t>(step) < m_pages.size())
        m_stack->set_active_widget(m_pages[static_cast<int>(step)]);
    if (m_sidebar) {
        int sidebar_step = static_cast<int>(step);
        if (step == Step::Finished)
            sidebar_step = static_cast<int>(Step::Summary);
        m_sidebar->set_current(sidebar_step);
    }

    // The live session can start before a hot-plugged disk is enumerated.
    // Try once more when the Disk page is actually reached; the page also has
    // an explicit Refresh disks button.
    if (step == Step::Disk && m_disks.is_empty())
        refresh_devices();

    if (step == Step::Mounts)
        update_mount_table();
    if (step == Step::Timezone)
        update_timezone_display();
    if (step == Step::Summary)
        update_summary();

    ByteString hint;
    switch (step) {
    case Step::Welcome:
        hint = "Select Next to begin."sv;
        break;
    case Step::Disk:
        hint = "Choose the target disk and layout."sv;
        break;
    case Step::Mounts:
        hint = "Assign a root filesystem and any other mount points."sv;
        break;
    case Step::Bootloader:
        hint = "Choose how the computer will start Halcyon Linux."sv;
        break;
    case Step::System:
        hint = "Set the computer name and keyboard layout."sv;
        break;
    case Step::Timezone:
        hint = "Click the map or choose a time zone from the list."sv;
        break;
    case Step::Accounts:
        hint = "Set the root password and create user accounts."sv;
        break;
    case Step::Desktop:
        hint = "Choose whether to install the Serenity Desktop Environment."sv;
        break;
    case Step::Options:
        hint = "Choose post-installation services and tasks."sv;
        break;
    case Step::Summary:
        hint = "Review the summary, then press Install."sv;
        break;
    case Step::Progress:
        hint = "Installation is in progress. Do not remove the installation media."sv;
        break;
    case Step::Finished:
        hint = "Remove the installation media, then restart the computer."sv;
        break;
    case Step::__Count:
        break;
    }
    m_footer_hint->set_text(to_string(hint));
    update_buttons();
}

void MainWindow::update_buttons()
{
    bool on_welcome = m_step == Step::Welcome;
    bool on_progress = m_step == Step::Progress;
    bool on_finished = m_step == Step::Finished;

    m_back_button->set_enabled(!on_welcome && !on_progress && !on_finished);
    m_next_button->set_enabled(!on_progress && !on_finished);
    m_cancel_button->set_enabled(!on_progress && !on_finished);

    if (m_step == Step::Summary)
        m_next_button->set_text("Install"_string);
    else if (on_finished)
        m_next_button->set_text("Finish"_string);
    else
        m_next_button->set_text("Next >"_string);

    bool custom = m_custom_layout_radio && m_custom_layout_radio->is_checked();
    bool has_partition = selected_mount_row() >= 0;
    if (m_set_mount_button)
        m_set_mount_button->set_enabled(custom && has_partition);
    if (m_format_button)
        m_format_button->set_enabled(custom && has_partition);
    if (m_mount_editor_button)
        m_mount_editor_button->set_enabled(selected_disk() != nullptr);
    if (m_mount_refresh_button)
        m_mount_refresh_button->set_enabled(true);
    if (m_partition_editor_button)
        m_partition_editor_button->set_enabled(selected_disk() != nullptr);
    if (m_refresh_devices_button)
        m_refresh_devices_button->set_enabled(true);
}

void MainWindow::set_selected_mountpoint()
{
    auto row_index = selected_mount_row();
    auto* row = m_mount_model ? m_mount_model->row_at(row_index) : nullptr;
    if (!row)
        return;

    auto result = show_mountpoint_dialog(*m_window, row->partition, row->size_bytes, row->mountpoint);
    if (!result.has_value())
        return;

    row->mountpoint = result.value();
    if (row->mountpoint == "swap"sv) {
        row->fs_type = "swap"sv;
        row->format = true;
    }
    m_mount_model->set_rows(m_mount_model->rows());
    update_buttons();
}

void MainWindow::format_selected_partition()
{
    auto row_index = selected_mount_row();
    auto* row = m_mount_model ? m_mount_model->row_at(row_index) : nullptr;
    if (!row)
        return;

    MountAssignment assignment;
    assignment.partition = row->partition;
    assignment.mountpoint = row->mountpoint;
    assignment.fs_type = row->fs_type;
    assignment.label = row->label;
    assignment.format = row->format;

    auto result = show_format_dialog(*m_window, assignment);
    if (!result.has_value())
        return;

    row->fs_type = result->fs_type;
    row->label = result->label;
    row->format = true;
    m_mount_model->set_rows(m_mount_model->rows());
    update_buttons();
}

void MainWindow::add_user()
{
    auto user = show_add_user_dialog(*m_window);
    if (!user.has_value())
        return;

    for (auto const& existing : m_users) {
        if (existing.username == user->username) {
            auto message = ByteString::formatted("The user name '{}' is already used.", user->username);
            show_warning(*m_window, message.view());
            return;
        }
    }

    m_users.append(user.release_value());
    update_user_model();
    update_buttons();
}

void MainWindow::remove_user()
{
    auto index = selected_user_row();
    if (index < 0 || static_cast<size_t>(index) >= m_users.size())
        return;
    m_users.remove(index);
    update_user_model();
    update_buttons();
}

void MainWindow::launch_partition_editor()
{
    auto const* disk = selected_disk();
    if (!disk) {
        show_warning(*m_window, "Select a disk before opening Partition Editor."sv);
        return;
    }
    if (m_editor_pid.has_value()) {
        show_warning(*m_window, "Partition Editor is already running."sv);
        return;
    }

    auto program = resolve_command("PartitionEditor"sv);
    if (program.is_empty())
        program = resolve_command("sparted"sv);
    if (program.is_empty()) {
        show_warning(*m_window, "Partition Editor was not found. Install the sparted package."sv);
        return;
    }

    Vector<ByteString> arguments;
    arguments.append("--device"sv);
    arguments.append(disk->path);
    auto result = Core::Process::spawn(program, arguments, {}, Core::Process::KeepAsChild::Yes);
    if (result.is_error()) {
        auto message = ByteString::formatted("Could not start Partition Editor: {}", result.error());
        show_warning(*m_window, message.view());
        return;
    }

    m_editor_pid = result.value();
    m_editor_timer->start(500);
    if (m_partition_editor_button)
        m_partition_editor_button->set_enabled(false);
    if (m_mount_editor_button)
        m_mount_editor_button->set_enabled(false);
}

void MainWindow::refresh_after_editor()
{
    if (!m_editor_pid.has_value()) {
        m_editor_timer->stop();
        return;
    }

    int status = 0;
    auto waited = waitpid(m_editor_pid.value(), &status, WNOHANG);
    if (waited == 0)
        return;

    m_editor_pid.clear();
    m_editor_timer->stop();
    refresh_devices();
}

void MainWindow::start_install()
{
    if (!validate_all())
        return;

    auto config = build_config();

    auto confirmation = GUI::MessageBox::show(m_window,
        ByteString::formatted("Install Halcyon Linux on {}?\n\nThis will write partition tables and filesystems. Data on the selected device or partitions may be lost.",
            config.target_device),
        "Begin Installation"sv, GUI::MessageBox::Type::Warning, GUI::MessageBox::InputType::YesNo);
    if (confirmation != GUI::Dialog::ExecResult::Yes)
        return;

    if (m_demo) {
        m_finished_success = true;
        m_finished_message = "Demo mode: no changes were made."sv;
        if (m_finished_title)
            m_finished_title->set_text("Setup complete"_string);
        if (m_finished_text)
            m_finished_text->set_text("Demo mode is active. The wizard did not write to any disk."_string);
        set_page(Step::Finished);
        return;
    }

    m_install_state.with_locked([](InstallState& state) {
        state = InstallState {};
    });
    m_cancel_requested.store(false);
    m_last_log_count = 0;
    m_log_text = {};
    m_log_view->set_text(""_string);
    m_progress_bar->set_value(0);
    m_progress_status->set_text("Starting installation..."_string);
    m_installing = true;

    set_page(Step::Progress);
    m_cancel_button->set_enabled(true);

    m_install_thread = Threading::Thread::construct([this, config = move(config), &state = m_install_state]() -> intptr_t {
        InstallCallbacks callbacks {};
        callbacks.on_log = [&state](ByteString const& line) {
            state.with_locked([&](InstallState& current) {
                current.log.append(line);
            });
        };
        callbacks.on_progress = [&state](int percent, ByteString const& status) {
            state.with_locked([&](InstallState& current) {
                current.percent = percent;
                current.status = status;
            });
        };
        callbacks.is_cancelled = [this] {
            return m_cancel_requested.load();
        };

        auto result = run_install(config, move(callbacks));
        state.with_locked([&](InstallState& current) {
            current.finished = true;
            current.success = result.success;
            current.message = result.message;
            current.percent = 100;
        });
        return 0;
    });
    m_install_thread->start();
    m_install_timer->start(150);
}

void MainWindow::poll_install()
{
    InstallState state;
    m_install_state.with_locked([&](InstallState const& current) {
        state = current;
    });

    if (!m_log_view)
        return;

    for (size_t index = m_last_log_count; index < state.log.size(); ++index) {
        if (!m_log_text.is_empty())
            m_log_text = ByteString::formatted("{}\n", m_log_text);
        m_log_text = ByteString::formatted("{}{}", m_log_text, state.log[index]);
    }
    m_last_log_count = state.log.size();
    if (!m_log_text.is_empty()) {
        m_log_view->set_text(m_log_text.view());
        // set_text() resets the cursor to the first line; follow the most
        // recent install output instead of leaving the view at the top.
        auto line_count = m_log_view->document().line_count();
        if (line_count > 0) {
            m_log_view->set_cursor(line_count - 1, 0);
            m_log_view->scroll_cursor_into_view();
        }
    }

    m_progress_bar->set_value(state.percent);
    if (!state.status.is_empty())
        m_progress_status->set_text(to_string(state.status));

    if (!state.finished)
        return;

    m_install_timer->stop();
    if (m_install_thread) {
        if (m_install_thread->needs_to_be_joined())
            (void)m_install_thread->join();
        m_install_thread = nullptr;
    }
    m_installing = false;
    m_finished_success = state.success;
    m_finished_message = state.message;

    if (m_finished_title)
        m_finished_title->set_text(state.success ? "Setup complete"_string : "Installation failed"_string);
    if (m_finished_text) {
        if (state.success) {
            m_finished_text->set_text(MUST(String::formatted(
                "Halcyon Linux has been installed successfully.\n\nRemove the installation media and restart the computer. "
                "The system will boot with {} into the user account you created.", bootloader_name(build_config().bootloader))));
        } else {
            m_finished_text->set_text(MUST(String::formatted(
                "The installation did not complete:\n\n{}\n\nReview the installation log, correct the problem, and try again.", state.message)));
        }
    }

    set_page(Step::Finished);
}


void MainWindow::cancel_install()
{
    auto result = GUI::MessageBox::show(m_window,
        "Cancel the installation?\n\nThe installer stops after the current operation. The target disk may be left in an incomplete state."sv,
        "Cancel Installation"sv, GUI::MessageBox::Type::Question, GUI::MessageBox::InputType::YesNo);
    if (result != GUI::Dialog::ExecResult::Yes)
        return;

    m_cancel_requested.store(true);
    m_progress_status->set_text("Cancelling after the current operation..."_string);
}

void MainWindow::show_page_for_testing(StringView name)
{
    warnln("Halcyon Setup: test page request '{}'", name);
    if (name == "welcome"sv)
        set_page(Step::Welcome);
    else if (name == "disk"sv)
        set_page(Step::Disk);
    else if (name == "mounts"sv)
        set_page(Step::Mounts);
    else if (name == "bootloader"sv)
        set_page(Step::Bootloader);
    else if (name == "system"sv)
        set_page(Step::System);
    else if (name == "timezone"sv)
        set_page(Step::Timezone);
    else if (name == "accounts"sv)
        set_page(Step::Accounts);
    else if (name == "desktop"sv)
        set_page(Step::Desktop);
    else if (name == "options"sv)
        set_page(Step::Options);
    else if (name == "summary"sv)
        set_page(Step::Summary);
}

}
