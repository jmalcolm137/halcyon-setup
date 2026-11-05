/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Installer/Config.h"
#include "Installer/Devices.h"
#include "Installer/Installer.h"
#include "UI/MainWindow.h"
#include <AK/NumberFormat.h>
#include <LibCore/Timer.h>
#include <LibGUI/Application.h>
#include <LibGUI/MessageBox.h>
#include <LibGUI/Window.h>
#include <LibMain/Main.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

using namespace Halcyon;

namespace {

void print_usage()
{
    printf("Usage: halcyon-setup [OPTIONS]\n\n");
    printf("Graphical installation wizard for Halcyon Linux.\n\n");
    printf("Options:\n");
    printf("  --demo               Run with sample disks and do not touch the system\n");
    printf("  --list-devices       Print the detected block devices and exit\n");
    printf("  --dump-plan DEVICE   Print the default whole-device layout and exit\n");
    printf("  -h, --help           Show this help\n");
    printf("  --version            Show version information\n\n");
    printf("The setup wizard needs root privileges. The packaged desktop entry uses\n");
    printf("pkexec to authenticate before starting it.\n");
}

bool list_devices()
{
    auto probe = probe_devices();
    if (!probe.error.is_empty()) {
        fprintf(stderr, "halcyon-setup: %s\n", probe.error.characters());
        return false;
    }
    if (probe.disks.is_empty()) {
        printf("No block devices found.\n");
        return true;
    }

    printf("Firmware: %s\n\n", probe.uefi ? "UEFI" : "legacy BIOS");
    for (auto const& disk : probe.disks) {
        printf("%s  %s  %s%s\n",
            disk.path.characters(),
            human_readable_size(disk.size_bytes).to_byte_string().characters(),
            disk.model.is_empty() ? "unknown model" : disk.model.characters(),
            disk.removable ? "  [removable]" : "");
        printf("  table: %s\n", disk.table_type.is_empty() ? "none" : disk.table_type.characters());
        for (auto const& partition : disk.partitions) {
            printf("  %-20s %10s  %-10s %s\n",
                partition.path.characters(),
                human_readable_size(partition.size_bytes).to_byte_string().characters(),
                partition.fs_type.is_empty() ? "unformatted" : partition.fs_type.characters(),
                partition.label.characters());
        }
    }
    return true;
}

bool dump_plan(ByteString const& path)
{
    auto probe = probe_devices();
    for (auto const& disk : probe.disks) {
        if (disk.path != path)
            continue;
        auto plan = whole_device_plan(disk, probe.uefi);
        if (!plan.error.is_empty()) {
            fprintf(stderr, "halcyon-setup: %s\n", plan.error.characters());
            return false;
        }
        printf("%s", describe_plan(plan.partitions, probe.uefi).characters());
        return true;
    }
    fprintf(stderr, "halcyon-setup: no such device: %s\n", path.characters());
    return false;
}

}

ErrorOr<int> serenity_main(Main::Arguments arguments)
{
    bool demo = false;
    for (size_t i = 1; i < arguments.strings.size(); ++i) {
        auto argument = arguments.strings[i];
        if (argument == "--help"sv || argument == "-h"sv) {
            print_usage();
            return 0;
        }
        if (argument == "--version"sv) {
            printf("Halcyon Setup 0.1.0\n");
            return 0;
        }
        if (argument == "--list-devices"sv)
            return list_devices() ? 0 : 1;
        if (argument == "--dump-plan"sv) {
            if (i + 1 >= arguments.strings.size()) {
                fprintf(stderr, "halcyon-setup: --dump-plan needs a device path\n");
                return 1;
            }
            return dump_plan(ByteString { arguments.strings[++i] }) ? 0 : 1;
        }
        if (argument == "--demo"sv) {
            demo = true;
            continue;
        }
        if (argument.starts_with("--"sv))
            fprintf(stderr, "halcyon-setup: ignoring unknown option %s\n", ByteString { argument }.characters());
    }

    auto app = TRY(GUI::Application::create(arguments));

    if (!demo && geteuid() != 0) {
        GUI::MessageBox::show(nullptr,
            "Halcyon Setup needs administrator privileges to write to disks.\n\n"
            "Start it from the desktop icon, which authenticates with pkexec, or run it as root."sv,
            "Halcyon Linux Setup"sv, GUI::MessageBox::Type::Warning);
        return 1;
    }

    auto main_window = TRY(Halcyon::MainWindow::create(demo));
    main_window->show();

    if (auto const* page = getenv("HALCYON_SETUP_TEST_PAGE"); page && *page) {
        auto timer = Core::Timer::create_single_shot(350, [window = main_window.ptr(), page = ByteString { page }] {
            warnln("Halcyon Setup: switching to test page {}", page);
            window->show_page_for_testing(page.view());
        }, app.ptr());
        timer->start();
    }

    warnln("Halcyon Setup: presented window");
    return app->exec();
}
