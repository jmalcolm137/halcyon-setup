/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Config.h"
#include <AK/StringBuilder.h>
#include <unistd.h>

namespace Halcyon {

static constexpr u64 s_mib = 1024ULL * 1024ULL;
static constexpr u64 s_gib = 1024ULL * s_mib;
static constexpr u64 s_boot_partition_size = 2 * s_gib;

PlanResult whole_device_plan(DiskInfo const& disk, bool uefi)
{
    PlanResult result;

    if (disk.path.is_empty()) {
        result.error = "No target device was selected."sv;
        return result;
    }
    if (disk.read_only) {
        result.error = "The selected device is read-only."sv;
        return result;
    }

    // 1 MiB is reserved at the front for the partition table and alignment.
    u64 cursor = s_mib;
    if (!uefi)
        cursor += s_mib;

    if (disk.size_bytes < cursor + s_boot_partition_size + s_gib) {
        result.error = "The selected device is too small for the default layout."sv;
        return result;
    }

    int number = 1;
    if (!uefi) {
        PlannedPartition bios;
        bios.number = number++;
        bios.path = partition_path_for(disk.path, bios.number);
        bios.start_bytes = s_mib;
        bios.size_bytes = s_mib;
        bios.fs_type = {};
        bios.label = "BIOS boot"sv;
        bios.bios_boot = true;
        bios.format = false;
        result.partitions.append(move(bios));
    }

    PlannedPartition boot;
    boot.number = number++;
    boot.path = partition_path_for(disk.path, boot.number);
    boot.start_bytes = cursor;
    boot.size_bytes = s_boot_partition_size;
    boot.fs_type = "vfat"sv;
    boot.mountpoint = "/boot"sv;
    boot.label = "HALCYON_BT"sv;
    boot.esp = uefi;
    boot.format = true;
    result.partitions.append(move(boot));
    cursor += s_boot_partition_size;

    PlannedPartition root;
    root.number = number;
    root.path = partition_path_for(disk.path, root.number);
    root.start_bytes = cursor;
    root.size_bytes = disk.size_bytes - cursor;
    root.fs_type = "bcachefs"sv;
    root.mountpoint = "/"sv;
    root.label = "HALCYON_ROOT"sv;
    root.format = true;
    result.partitions.append(move(root));

    return result;
}

Vector<MountAssignment> mounts_from_plan(Vector<PlannedPartition> const& plan)
{
    Vector<MountAssignment> mounts;
    for (auto const& partition : plan) {
        if (partition.mountpoint.is_empty())
            continue;
        MountAssignment mount;
        mount.partition = partition.path;
        mount.mountpoint = partition.mountpoint;
        mount.fs_type = partition.fs_type;
        mount.label = partition.label;
        mount.format = partition.format;
        mounts.append(move(mount));
    }
    return mounts;
}

ByteString bootloader_name(Bootloader bootloader)
{
    switch (bootloader) {
    case Bootloader::Limine:
        return "Limine"sv;
    case Bootloader::Grub:
        return "GRUB"sv;
    }
    return "Limine"sv;
}

bool filesystem_is_fat(StringView fs_type)
{
    return fs_type == "vfat"sv || fs_type == "fat"sv || fs_type == "fat16"sv || fs_type == "fat32"sv;
}

bool filesystem_is_bootloader_readable(StringView fs_type)
{
    return filesystem_is_fat(fs_type) || fs_type == "ext2"sv || fs_type == "ext3"sv || fs_type == "ext4"sv;
}

ByteString filesystem_display_name(StringView fs_type)
{
    if (filesystem_is_fat(fs_type))
        return "FAT32"sv;
    if (fs_type == "bcachefs"sv)
        return "bcachefs"sv;
    if (fs_type == "ext2"sv || fs_type == "ext3"sv || fs_type == "ext4"sv)
        return ByteString { fs_type };
    if (fs_type == "btrfs"sv)
        return "btrfs"sv;
    if (fs_type == "xfs"sv)
        return "XFS"sv;
    if (fs_type == "f2fs"sv)
        return "F2FS"sv;
    if (fs_type == "swap"sv || fs_type == "linux-swap"sv)
        return "swap"sv;
    if (fs_type.is_empty())
        return "unformatted"sv;
    return ByteString { fs_type };
}

static bool valid_hostname(StringView hostname)
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

static bool valid_username(StringView username)
{
    if (username.is_empty() || username.length() > 32)
        return false;
    if (username == "root"sv)
        return false;
    auto first = username[0];
    if (!((first >= 'a' && first <= 'z') || first == '_'))
        return false;
    for (auto character : username) {
        if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' || character == '-')
            continue;
        return false;
    }
    return true;
}

static bool valid_password(StringView password)
{
    if (password.is_empty())
        return false;
    for (auto character : password) {
        if (character == ':' || character == '\n' || character == '\r' || character == '\0')
            return false;
    }
    return true;
}

ByteString validate_config(InstallConfig const& config, Vector<DiskInfo> const& disks)
{
    if (config.target_device.is_empty())
        return "No installation device has been selected."sv;

    DiskInfo const* disk = nullptr;
    for (auto const& candidate : disks) {
        if (candidate.path == config.target_device) {
            disk = &candidate;
            break;
        }
    }
    if (!disk)
        return "The selected device is no longer present."sv;
    if (disk->read_only)
        return "The selected device is read-only."sv;

    if (config.mounts.is_empty())
        return "No partitions have been assigned mount points."sv;

    bool saw_root = false;
    bool saw_boot = false;
    bool saw_esp = false;
    bool boot_is_fat = false;
    bool esp_is_fat = false;
    ByteString root_fs;
    ByteString boot_fs;

    for (auto const& mount : config.mounts) {
        if (mount.mountpoint.is_empty())
            continue;

        auto const* partition = disk->partition_by_path(mount.partition);
        if (!partition && config.layout == LayoutMode::Custom)
            return ByteString::formatted("The partition {} does not belong to {}.", mount.partition, config.target_device);

        if (mount.mountpoint != "swap"sv && !mount.mountpoint.starts_with('/'))
            return ByteString::formatted("The mount point '{}' is not an absolute path.", mount.mountpoint);

        if (mount.mountpoint == "/"sv) {
            if (saw_root)
                return "More than one partition is assigned to /."sv;
            saw_root = true;
            root_fs = mount.fs_type;
            if (root_fs.is_empty())
                return "The root partition needs a filesystem."sv;
            if (root_fs == "swap"sv || filesystem_is_fat(root_fs) || root_fs == "ntfs"sv || root_fs == "exfat"sv)
                return "The root filesystem must be a Linux filesystem such as bcachefs, ext4, btrfs or XFS."sv;
        }

        if (mount.mountpoint == "/boot"sv) {
            saw_boot = true;
            boot_fs = mount.fs_type;
            boot_is_fat = filesystem_is_fat(mount.fs_type);
        }
        if (mount.mountpoint == "/boot/efi"sv) {
            saw_esp = true;
            esp_is_fat = filesystem_is_fat(mount.fs_type);
        }
        if (mount.mountpoint != "swap"sv && mount.fs_type.is_empty())
            return ByteString::formatted("The partition for {} needs a filesystem.", mount.mountpoint);
        if (mount.mountpoint == "/usr"sv || mount.mountpoint.starts_with("/usr/"sv))
            return "A separate /usr is not supported; Chimera and Halcyon are merged-/usr systems."sv;
    }

    for (size_t i = 0; i < config.mounts.size(); ++i) {
        for (size_t j = i + 1; j < config.mounts.size(); ++j) {
            if (config.mounts[i].mountpoint.is_empty() || config.mounts[j].mountpoint.is_empty())
                continue;
            if (config.mounts[i].mountpoint == config.mounts[j].mountpoint)
                return ByteString::formatted("Two partitions are assigned to {}.", config.mounts[i].mountpoint);
        }
    }

    if (!saw_root)
        return "One partition must be assigned to /."sv;

    if (config.bootloader == Bootloader::Limine) {
        if (!saw_boot)
            return "Limine needs a separate /boot partition formatted as FAT32."sv;
        if (!boot_is_fat)
            return "The /boot partition must be FAT32 when Limine is selected."sv;
    } else {
        if (!saw_boot && !filesystem_is_bootloader_readable(root_fs))
            return ByteString::formatted("GRUB cannot read the {} root filesystem; create a separate /boot partition.", filesystem_display_name(root_fs));
    }

    if (config.uefi) {
        if (!(saw_boot && boot_is_fat) && !(saw_esp && esp_is_fat))
            return "UEFI systems need a FAT32 EFI system partition mounted at /boot or /boot/efi."sv;
    }

    if (!valid_hostname(config.hostname))
        return "Enter a valid hostname (letters, digits, dots and hyphens; no leading or trailing hyphen)."sv;

    if (config.timezone.is_empty())
        return "Select a time zone."sv;

    if (!valid_password(config.root_password))
        return "Enter a root password. It cannot be empty or contain a newline or colon."sv;

    Vector<ByteString> usernames;
    for (auto const& user : config.users) {
        if (!valid_username(user.username))
            return ByteString::formatted("'{}' is not a valid user name.", user.username);
        if (!valid_password(user.password))
            return ByteString::formatted("Enter a password for {} that does not contain a newline or colon.", user.username);
        for (auto const& existing : usernames) {
            if (existing == user.username)
                return ByteString::formatted("The user name '{}' is used more than once.", user.username);
        }
        usernames.append(user.username);
    }

    return {};
}

}
