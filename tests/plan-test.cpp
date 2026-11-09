/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "../src/Installer/Config.h"
#include <AK/ByteString.h>
#include <stdio.h>

using namespace Halcyon;

static int failures = 0;

static void check(bool condition, char const* message)
{
    if (condition)
        return;
    fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

static DiskInfo make_disk()
{
    DiskInfo disk;
    disk.path = "/dev/nvme0n1"sv;
    disk.name = "nvme0n1"sv;
    disk.model = "Test disk"sv;
    disk.size_bytes = 256ULL * 1024 * 1024 * 1024;
    disk.table_type = "gpt"sv;
    disk.has_table = true;
    return disk;
}

int main()
{
    auto disk = make_disk();
    Vector<DiskInfo> disks;
    disks.append(disk);
    auto uefi_plan = whole_device_plan(disk, true);
    check(uefi_plan.error.is_empty(), "UEFI plan has no error");
    check(uefi_plan.partitions.size() == 2, "UEFI plan has two partitions");
    if (uefi_plan.partitions.size() == 2) {
        check(uefi_plan.partitions[0].esp, "UEFI first partition is the ESP");
        check(uefi_plan.partitions[0].fs_type == "vfat"sv, "UEFI ESP is FAT32");
        check(uefi_plan.partitions[0].size_bytes == 2ULL * 1024 * 1024 * 1024, "UEFI ESP is 2 GiB");
        check(uefi_plan.partitions[1].fs_type == "bcachefs"sv, "UEFI root is bcachefs");
    }

    auto bios_plan = whole_device_plan(disk, false);
    check(bios_plan.error.is_empty(), "BIOS plan has no error");
    check(bios_plan.partitions.size() == 3, "BIOS plan has three partitions");
    if (bios_plan.partitions.size() == 3) {
        check(bios_plan.partitions[0].bios_boot, "BIOS first partition is the BIOS boot partition");
        check(filesystem_is_fat(bios_plan.partitions[1].fs_type), "BIOS /boot is FAT32");
        check(bios_plan.partitions[1].mountpoint == "/boot"sv, "BIOS /boot mount point");
        check(bios_plan.partitions[2].mountpoint == "/"sv, "BIOS root mount point");
    }

    InstallConfig config;
    config.layout = LayoutMode::EntireDevice;
    config.target_device = disk.path;
    config.uefi = true;
    config.mounts = mounts_from_plan(uefi_plan.partitions);
    config.hostname = "halcyon"sv;
    config.timezone = "Europe/Prague"sv;
    config.root_password = "correct horse battery staple"sv;
    config.bootloader = Bootloader::Limine;
    UserAccount user;
    user.username = "halcyon"sv;
    user.password = "correct horse battery staple"sv;
    config.users.append(user);
    {
        auto error = validate_config(config, disks);
        if (!error.is_empty())
            fprintf(stderr, "validation error: %s\n", error.characters());
        check(error.is_empty(), "default UEFI configuration validates");
    }

    // The Mounts page runs before System and Accounts. Its storage-only check
    // must not reject the not-yet-entered hostname, time zone or root password.
    auto storage_only = config;
    storage_only.hostname = {};
    storage_only.timezone = {};
    storage_only.root_password = {};
    check(validate_storage_config(storage_only, disks).is_empty(), "mount validation ignores later-page fields");
    check(!validate_config(storage_only, disks).is_empty(), "full validation still requires later-page fields");

    auto broken = config;
    broken.mounts.clear();
    check(!validate_config(broken, disks).is_empty(), "missing root is rejected");

    auto no_esp = config;
    no_esp.mounts.clear();
    MountAssignment root;
    root.partition = "/dev/nvme0n1p2"sv;
    root.mountpoint = "/"sv;
    root.fs_type = "bcachefs"sv;
    no_esp.mounts.append(root);
    check(!validate_config(no_esp, disks).is_empty(), "UEFI without an ESP is rejected");

    auto ext4_boot = config;
    ext4_boot.mounts.clear();
    MountAssignment ext_boot;
    ext_boot.partition = "/dev/nvme0n1p1"sv;
    ext_boot.mountpoint = "/boot"sv;
    ext_boot.fs_type = "ext4"sv;
    ext_boot.format = true;
    ext4_boot.mounts.append(ext_boot);
    ext4_boot.mounts.append(root);
    check(!validate_config(ext4_boot, disks).is_empty(), "Limine with ext4 /boot is rejected");

    if (failures == 0)
        printf("plan tests passed\n");
    return failures == 0 ? 0 : 1;
}
