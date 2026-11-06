/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../Installer/Config.h"
#include "Models.h"
#include <AK/Atomic.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <AK/Vector.h>
#include <LibCore/Timer.h>
#include <LibGUI/Forward.h>
#include <LibGUI/Icon.h>
#include <LibGUI/Progressbar.h>
#include <LibGUI/TextBox.h>
#include <LibGUI/Window.h>
#include <LibThreading/MutexProtected.h>
#include <LibThreading/Thread.h>

namespace Halcyon {

class WorldMap;

class MainWindow {
public:
    static ErrorOr<NonnullOwnPtr<MainWindow>> create(bool demo = false);
    ~MainWindow();

    void show();
    void show_page_for_testing(StringView name);

private:
    enum class Step {
        Welcome = 0,
        Disk,
        Mounts,
        Bootloader,
        System,
        Timezone,
        Accounts,
        Desktop,
        Options,
        Summary,
        Progress,
        Finished,
        __Count,
    };

    explicit MainWindow(bool demo);

    ErrorOr<void> build_ui();
    void build_pages();
    void build_disk_page();
    void build_mounts_page();
    void build_bootloader_page();
    void build_system_page();
    void build_timezone_page();
    void build_accounts_page();
    void build_desktop_page();
    void build_options_page();
    void build_summary_page();
    void build_progress_page();
    void build_finished_page();

    void refresh_devices();
    void on_device_changed();
    void update_disk_preview();
    void update_mount_table();
    void update_user_model();
    void update_summary();
    void update_timezone_display();
    void update_buttons();

    void set_page(Step);
    bool validate_current_page();
    bool validate_all();
    InstallConfig build_config() const;

    DiskInfo const* selected_disk() const;
    int selected_mount_row() const;
    int selected_user_row() const;

    void set_timezone(ByteString const&);
    void launch_partition_editor();
    void refresh_after_editor();
    void set_selected_mountpoint();
    void format_selected_partition();
    void add_user();
    void remove_user();

    void start_install();
    void poll_install();
    void cancel_install();

    bool m_demo { false };

    RefPtr<GUI::Window> m_window;
    GUI::Icon m_app_icon;
    RefPtr<GUI::StackWidget> m_stack;
    RefPtr<class WizardSidebar> m_sidebar;
    RefPtr<GUI::Button> m_back_button;
    RefPtr<GUI::Button> m_next_button;
    RefPtr<GUI::Button> m_cancel_button;
    RefPtr<GUI::Label> m_footer_hint;

    Vector<GUI::Widget*> m_pages;
    Step m_step { Step::Welcome };

    Vector<DiskInfo> m_disks;
    Vector<ByteString> m_device_paths;
    Vector<ByteString> m_device_labels;
    bool m_uefi { false };
    PlanResult m_plan;
    ByteString m_probe_error;
    bool m_updating_device_combo { false };

    RefPtr<GUI::ComboBox> m_device_combo;
    RefPtr<GUI::RadioButton> m_entire_device_radio;
    RefPtr<GUI::RadioButton> m_custom_layout_radio;
    RefPtr<GUI::CheckBox> m_erase_confirm;
    RefPtr<GUI::Label> m_device_summary;
    RefPtr<GUI::Label> m_firmware_label;
    RefPtr<GUI::TextEditor> m_plan_preview;
    RefPtr<GUI::Button> m_partition_editor_button;
    RefPtr<GUI::Button> m_refresh_devices_button;

    RefPtr<MountTableModel> m_mount_model;
    RefPtr<GUI::TableView> m_mount_table;
    RefPtr<GUI::Label> m_mount_hint;
    RefPtr<GUI::Button> m_set_mount_button;
    RefPtr<GUI::Button> m_format_button;
    RefPtr<GUI::Button> m_mount_editor_button;
    RefPtr<GUI::Button> m_mount_refresh_button;

    RefPtr<GUI::RadioButton> m_limine_radio;
    RefPtr<GUI::RadioButton> m_grub_radio;

    RefPtr<GUI::TextBox> m_hostname_input;
    RefPtr<GUI::ComboBox> m_keyboard_combo;
    Vector<ByteString> m_keyboard_layouts;
    RefPtr<GUI::RadioButton> m_rtc_utc_radio;
    RefPtr<GUI::RadioButton> m_rtc_localtime_radio;

    RefPtr<GUI::ComboBox> m_timezone_combo;
    RefPtr<WorldMap> m_world_map;
    RefPtr<GUI::Label> m_timezone_label;
    Vector<ByteString> m_timezone_names;
    ByteString m_timezone { "UTC" };
    bool m_updating_timezone { false };

    RefPtr<GUI::PasswordBox> m_root_password;
    RefPtr<GUI::PasswordBox> m_root_confirm;
    RefPtr<UserTableModel> m_user_model;
    RefPtr<GUI::TableView> m_user_table;
    RefPtr<GUI::Label> m_user_hint;
    Vector<UserAccount> m_users;

    RefPtr<GUI::CheckBox> m_install_desktop_checkbox;
    RefPtr<GUI::CheckBox> m_remove_base_live_checkbox;
    RefPtr<GUI::CheckBox> m_update_system_checkbox;
    RefPtr<GUI::CheckBox> m_enable_syslog_checkbox;
    RefPtr<GUI::CheckBox> m_enable_networkmanager_checkbox;
    RefPtr<GUI::CheckBox> m_enable_ntp_checkbox;

    RefPtr<GUI::TextEditor> m_summary_view;
    RefPtr<GUI::CheckBox> m_final_confirm;

    RefPtr<GUI::HorizontalProgressbar> m_progress_bar;
    RefPtr<GUI::Label> m_progress_status;
    RefPtr<GUI::TextEditor> m_log_view;
    ByteString m_log_text;

    struct InstallState {
        bool finished { false };
        bool success { false };
        int percent { 0 };
        ByteString status;
        Vector<ByteString> log;
        ByteString message;
    };

    Threading::MutexProtected<InstallState> m_install_state;
    Atomic<bool> m_cancel_requested { false };
    NonnullRefPtr<Core::Timer> m_install_timer;
    NonnullRefPtr<Core::Timer> m_editor_timer;
    RefPtr<Threading::Thread> m_install_thread;
    bool m_installing { false };
    size_t m_last_log_count { 0 };
    Optional<pid_t> m_editor_pid;

    RefPtr<GUI::Label> m_finished_title;
    RefPtr<GUI::Label> m_finished_text;
    bool m_finished_success { false };
    ByteString m_finished_message;
};

}
