/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Devices.h"
#include "Command.h"
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <stdlib.h>
#include <unistd.h>

namespace Halcyon {

bool system_is_uefi()
{
    return access("/sys/firmware/efi", F_OK) == 0;
}

static ByteString string_or(JsonObject const& object, StringView key)
{
    auto value = object.get_byte_string(key);
    return value.value_or(ByteString {});
}

static bool bool_or(JsonObject const& object, StringView key, bool fallback = false)
{
    auto value = object.get_bool(key);
    return value.value_or(fallback);
}

static u64 number_or(JsonObject const& object, StringView key, u64 fallback = 0)
{
    auto value = object.get_u64(key);
    return value.value_or(fallback);
}

static Vector<ByteString> string_array(JsonObject const& object, StringView key)
{
    Vector<ByteString> values;
    auto maybe_array = object.get_array(key);
    if (!maybe_array.has_value())
        return values;

    for (auto const& item : maybe_array->values()) {
        if (item.is_string())
            values.append(item.as_string());
    }
    return values;
}

static PartitionInfo parse_partition(JsonObject const& object)
{
    PartitionInfo partition;
    partition.path = string_or(object, "path"sv);
    partition.name = string_or(object, "name"sv);
    partition.size_bytes = number_or(object, "size"sv);
    partition.fs_type = string_or(object, "fstype"sv);
    partition.fs_version = string_or(object, "fsver"sv);
    partition.label = string_or(object, "label"sv);
    partition.uuid = string_or(object, "uuid"sv);
    partition.partuuid = string_or(object, "partuuid"sv);
    partition.partlabel = string_or(object, "partlabel"sv);
    partition.mountpoints = string_array(object, "mountpoints"sv);
    partition.read_only = bool_or(object, "ro"sv);
    return partition;
}

DeviceProbe probe_devices()
{
    DeviceProbe probe;
    probe.uefi = system_is_uefi();

    auto result = run_command(
        "lsblk"sv,
        {
            "-J"sv,
            "-b"sv,
            "-p"sv,
            "-o"sv,
            "NAME,PATH,TYPE,SIZE,MODEL,SERIAL,RO,RM,FSTYPE,FSVER,LABEL,UUID,PARTUUID,PARTLABEL,MOUNTPOINTS,PTTYPE"sv,
        },
        {},
        20);

    if (!result.ok()) {
        probe.error = result.combined_output();
        if (probe.error.is_empty())
            probe.error = "Could not read the block-device list with lsblk."sv;
        return probe;
    }

    auto json = JsonValue::from_string(result.output);
    if (json.is_error()) {
        probe.error = "Could not parse the lsblk output."sv;
        return probe;
    }

    if (!json.value().is_object()) {
        probe.error = "The lsblk output did not contain a JSON object."sv;
        return probe;
    }
    auto const& root_object = json.value().as_object();

    auto maybe_devices = root_object.get_array("blockdevices"sv);
    if (!maybe_devices.has_value())
        return probe;

    for (auto const& entry : maybe_devices->values()) {
        if (!entry.is_object())
            continue;
        auto const& object = entry.as_object();
        auto type = string_or(object, "type"sv);
        if (type != "disk"sv && type != "loop"sv)
            continue;

        auto name = string_or(object, "name"sv);
        if (name.starts_with("/dev/zram"sv))
            continue;
        if (type == "loop"sv && getenv("HALCYON_SETUP_SHOW_LOOP") == nullptr)
            continue;

        DiskInfo disk;
        disk.path = string_or(object, "path"sv);
        disk.name = string_or(object, "name"sv);
        disk.model = string_or(object, "model"sv);
        disk.serial = string_or(object, "serial"sv);
        disk.size_bytes = number_or(object, "size"sv);
        disk.removable = bool_or(object, "rm"sv);
        disk.read_only = bool_or(object, "ro"sv);
        disk.table_type = string_or(object, "pttype"sv);
        disk.has_table = !disk.table_type.is_empty();

        auto maybe_children = object.get_array("children"sv);
        if (maybe_children.has_value()) {
            for (auto const& child : maybe_children->values()) {
                if (!child.is_object())
                    continue;
                auto child_object = child.as_object();
                if (string_or(child_object, "type"sv) != "part"sv)
                    continue;
                disk.partitions.append(parse_partition(child_object));
            }
        }

        probe.disks.append(move(disk));
    }

    return probe;
}

ByteString partition_path_for(ByteString const& disk_path, int number)
{
    if (disk_path.is_empty())
        return {};

    auto last = disk_path[disk_path.length() - 1];
    if (last >= '0' && last <= '9')
        return ByteString::formatted("{}p{}", disk_path, number);
    return ByteString::formatted("{}{}", disk_path, number);
}

static PartitionInfo demo_partition(ByteString path, u64 size, ByteString fs, ByteString label, ByteString uuid = {})
{
    PartitionInfo partition;
    partition.path = move(path);
    partition.size_bytes = size;
    partition.fs_type = move(fs);
    partition.label = move(label);
    partition.uuid = move(uuid);
    return partition;
}

Vector<DiskInfo> demo_devices()
{
    Vector<DiskInfo> disks;

    DiskInfo nvme;
    nvme.path = "/dev/nvme0n1"sv;
    nvme.name = "nvme0n1"sv;
    nvme.model = "Samsung SSD 990 PRO 2TB"sv;
    nvme.serial = "S6Z1NJ0W123456"sv;
    nvme.size_bytes = 2'000'398'934'016ULL;
    nvme.table_type = "gpt"sv;
    nvme.has_table = true;
    nvme.partitions.append(demo_partition("/dev/nvme0n1p1"sv, 536'870'912, "vfat"sv, "SYSTEM"sv, "A1B2-C3D4"sv));
    nvme.partitions.append(demo_partition("/dev/nvme0n1p2"sv, 99'000'000'000, "ntfs"sv, "Windows"sv, "0123456789ABCDEF"sv));
    disks.append(move(nvme));

    DiskInfo usb;
    usb.path = "/dev/sda"sv;
    usb.name = "sda"sv;
    usb.model = "Ultra USB 3.0"sv;
    usb.serial = "4C530001120322117292"sv;
    usb.size_bytes = 32'000'000'000ULL;
    usb.removable = true;
    usb.table_type = "dos"sv;
    usb.has_table = true;
    usb.partitions.append(demo_partition("/dev/sda1"sv, 32'000'000'000ULL, "ext4"sv, "BACKUP"sv, "8f3a0d9e-8b8c-4a4e-9d33-56b1c22e7711"sv));
    disks.append(move(usb));

    return disks;
}

}
