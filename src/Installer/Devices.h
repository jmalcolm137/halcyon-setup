/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Optional.h>
#include <AK/Vector.h>

namespace Halcyon {

struct PartitionInfo {
    ByteString path;
    ByteString name;
    u64 size_bytes { 0 };
    ByteString fs_type;
    ByteString fs_version;
    ByteString label;
    ByteString uuid;
    ByteString partuuid;
    ByteString partlabel;
    Vector<ByteString> mountpoints;
    bool read_only { false };
};

struct DiskInfo {
    ByteString path;
    ByteString name;
    ByteString model;
    ByteString serial;
    ByteString table_type;
    u64 size_bytes { 0 };
    bool removable { false };
    bool read_only { false };
    bool has_table { false };
    Vector<PartitionInfo> partitions;

    PartitionInfo const* partition_by_path(StringView path) const
    {
        for (auto const& partition : partitions) {
            if (partition.path == path)
                return &partition;
        }
        return nullptr;
    }
};

struct DeviceProbe {
    Vector<DiskInfo> disks;
    ByteString error;
    bool uefi { false };
};

bool system_is_uefi();

// Read block devices from lsblk. This is a read-only operation and does not
// need libparted or any other partition-table library.
DeviceProbe probe_devices();

// Generate /dev/sda1, /dev/nvme0n1p2, /dev/mmcblk0p2, etc.
ByteString partition_path_for(ByteString const& disk_path, int number);

// Add one or more synthetic devices for the UI demo test.
Vector<DiskInfo> demo_devices();

}
