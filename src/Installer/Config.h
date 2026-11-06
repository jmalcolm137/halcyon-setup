/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Devices.h"
#include <AK/ByteString.h>
#include <AK/Optional.h>
#include <AK/Vector.h>

namespace Halcyon {

enum class LayoutMode {
    EntireDevice,
    Custom,
};

enum class Bootloader {
    Limine,
    Grub,
};

struct MountAssignment {
    ByteString partition;
    ByteString mountpoint;
    ByteString fs_type;
    ByteString label;
    bool format { true };
};

struct UserAccount {
    ByteString username;
    ByteString full_name;
    ByteString password;
    Vector<ByteString> groups;
};

struct InstallConfig {
    LayoutMode layout { LayoutMode::EntireDevice };
    ByteString target_device;
    bool uefi { false };

    Vector<MountAssignment> mounts;

    ByteString hostname { "halcyon"sv };
    ByteString timezone { "UTC"sv };
    ByteString keyboard_layout { "us"sv };
    ByteString keyboard_variant;
    bool hardware_clock_localtime { false };

    Bootloader bootloader { Bootloader::Limine };

    bool install_desktop { true };
    bool remove_base_live { true };
    bool update_system { true };
    bool enable_syslog { true };
    bool enable_networkmanager { true };
    bool enable_ntp { true };

    ByteString root_password;
    Vector<UserAccount> users;
};

struct PlannedPartition {
    int number { 0 };
    ByteString path;
    u64 start_bytes { 0 };
    u64 size_bytes { 0 };
    ByteString fs_type;
    ByteString mountpoint;
    ByteString label;
    bool bios_boot { false };
    bool esp { false };
    bool format { true };
};

struct PlanResult {
    Vector<PlannedPartition> partitions;
    ByteString error;
};

// The default whole-device layout. UEFI systems get a 2 GiB FAT32 partition
// for /boot (also the ESP) and the rest of the disk is a single bcachefs root.
// BIOS systems additionally get a 1 MiB BIOS-boot partition; the FAT32 /boot
// partition is still required by the bootloaders that can read a kernel from
// a non-bcachefs filesystem.
PlanResult whole_device_plan(DiskInfo const& disk, bool uefi);

Vector<MountAssignment> mounts_from_plan(Vector<PlannedPartition> const& plan);

// Returns an empty string when the configuration is valid, otherwise a
// user-facing explanation of the first problem.
ByteString validate_config(InstallConfig const&, Vector<DiskInfo> const& disks);

// Validates only the disk, layout and mount assignment. The Mounts step uses
// this so fields entered on later pages (hostname, time zone, accounts) do not
// block navigation before those pages exist.
ByteString validate_storage_config(InstallConfig const&, Vector<DiskInfo> const& disks);

ByteString bootloader_name(Bootloader);
ByteString filesystem_display_name(StringView fs_type);
bool filesystem_is_fat(StringView fs_type);
bool filesystem_is_bootloader_readable(StringView fs_type);

}
