/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Installer.h"
#include "Command.h"
#include <AK/NumberFormat.h>
#include <AK/StringBuilder.h>
#include <AK/Vector.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace Halcyon {

static ByteString join_arguments(Vector<ByteString> const& arguments)
{
    StringBuilder builder;
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (i > 0)
            builder.append(' ');
        builder.append(arguments[i]);
    }
    return builder.to_byte_string();
}

static bool make_path(ByteString const& path)
{
    if (path.is_empty())
        return false;

    auto current = ByteString {};
    size_t index = 0;
    if (path.starts_with('/')) {
        current = "/"sv;
        index = 1;
    }

    while (index <= path.length()) {
        auto slash = path.find('/', index);
        auto end = slash.value_or(path.length());
        auto component = path.substring(index, end - index);
        if (!component.is_empty()) {
            if (current == "/"sv)
                current = ByteString::formatted("/{}", component);
            else if (current.is_empty())
                current = component;
            else
                current = ByteString::formatted("{}/{}", current, component);

            if (mkdir(current.characters(), 0755) != 0 && errno != EEXIST)
                return false;
        }
        if (!slash.has_value())
            break;
        index = *slash + 1;
    }
    return true;
}

static ByteString read_file(ByteString const& path)
{
    auto fd = open(path.characters(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return {};

    StringBuilder builder;
    char buffer[8192];
    for (;;) {
        auto bytes = read(fd, buffer, sizeof(buffer));
        if (bytes < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (bytes == 0)
            break;
        builder.append(StringView { buffer, static_cast<size_t>(bytes) });
    }
    close(fd);
    return builder.to_byte_string();
}

static bool write_file(ByteString const& path, StringView contents, mode_t mode = 0644)
{
    auto parent_end = path.find_last('/');
    if (parent_end.has_value() && *parent_end > 0) {
        if (!make_path(path.substring(0, *parent_end)))
            return false;
    }

    auto fd = open(path.characters(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0)
        return false;

    auto remaining = contents;
    while (!remaining.is_empty()) {
        auto written = write(fd, remaining.characters_without_null_termination(), remaining.length());
        if (written < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            return false;
        }
        remaining = remaining.substring_view(static_cast<size_t>(written));
    }
    close(fd);
    return true;
}

static bool copy_file(ByteString const& source, ByteString const& destination, mode_t mode = 0644)
{
    auto input = open(source.characters(), O_RDONLY | O_CLOEXEC);
    if (input < 0)
        return false;

    auto parent_end = destination.find_last('/');
    if (parent_end.has_value() && *parent_end > 0)
        make_path(destination.substring(0, *parent_end));

    auto output = open(destination.characters(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (output < 0) {
        close(input);
        return false;
    }

    char buffer[65536];
    bool ok = true;
    for (;;) {
        auto bytes = read(input, buffer, sizeof(buffer));
        if (bytes < 0) {
            if (errno == EINTR)
                continue;
            ok = false;
            break;
        }
        if (bytes == 0)
            break;

        auto offset = 0;
        while (offset < bytes) {
            auto written = write(output, buffer + offset, static_cast<size_t>(bytes - offset));
            if (written < 0) {
                if (errno == EINTR)
                    continue;
                ok = false;
                break;
            }
            offset += written;
        }
        if (!ok)
            break;
    }

    close(input);
    close(output);
    if (ok)
        chmod(destination.characters(), mode);
    return ok;
}

static bool replace_symlink(ByteString const& target, ByteString const& link_path)
{
    unlink(link_path.characters());
    return symlink(target.characters(), link_path.characters()) == 0;
}

static ByteString mount_target(ByteString const& target, StringView mountpoint)
{
    if (mountpoint == "/"sv)
        return target;
    return ByteString::formatted("{}{}", target, mountpoint);
}

static bool path_is_mounted(ByteString const& path)
{
    auto mounts = read_file("/proc/mounts"sv);
    if (mounts.is_empty())
        return false;

    for (auto line : mounts.split('\n')) {
        auto fields = line.split_view(' ');
        if (fields.size() < 2)
            continue;
        if (fields[1] == path)
            return true;
    }
    return false;
}

static bool device_is_mounted(ByteString const& path)
{
    auto mounts = read_file("/proc/mounts"sv);
    if (mounts.is_empty())
        return false;

    for (auto line : mounts.split('\n')) {
        auto fields = line.split_view(' ');
        if (fields.is_empty())
            continue;
        if (fields[0] == path)
            return true;
    }
    return false;
}

static void append_modules_if_missing(ByteString const& target, StringView module)
{
    auto path = ByteString::formatted("{}/etc/initramfs-tools/modules", target);
    auto existing = read_file(path);
    for (auto line : existing.split('\n')) {
        auto trimmed = line.trim_whitespace();
        if (trimmed == module)
            return;
    }

    auto contents = existing;
    if (!contents.is_empty() && !contents.ends_with('\n'))
        contents = ByteString::formatted("{}\n", contents);
    contents = ByteString::formatted("{}{}\n", contents, module);
    write_file(path, contents);
}

static ByteString machine_architecture()
{
    struct utsname name {};
    if (uname(&name) != 0)
        return {};
    return ByteString { name.machine };
}

static ByteString root_uuid_for(ByteString const& partition)
{
    auto result = run_command("blkid"sv, { "-s"sv, "UUID"sv, "-o"sv, "value"sv, partition }, {}, 10);
    if (!result.ok())
        return {};
    return ByteString { result.output.trim_whitespace() };
}

static Vector<ByteString> list_directory(ByteString const& path)
{
    Vector<ByteString> entries;
    auto* directory = opendir(path.characters());
    if (!directory)
        return entries;

    while (auto* entry = readdir(directory)) {
        if (entry->d_name[0] == '.')
            continue;
        entries.append(ByteString { entry->d_name });
    }
    closedir(directory);
    return entries;
}

struct KernelEntry {
    ByteString version;
    ByteString kernel;
    ByteString initrd;
};

static ByteString kernel_version_from_modules(ByteString const& target)
{
    for (auto root : { "/usr/lib/modules"sv, "/lib/modules"sv }) {
        auto directory = ByteString::formatted("{}{}", target, root);
        for (auto const& name : list_directory(directory)) {
            auto path = ByteString::formatted("{}/{}", directory, name);
            struct stat status;
            if (stat(path.characters(), &status) == 0 && S_ISDIR(status.st_mode))
                return name;
        }
    }
    return {};
}

static Vector<KernelEntry> find_kernels(ByteString const& target)
{
    Vector<KernelEntry> kernels;
    auto boot = ByteString::formatted("{}/boot", target);
    auto names = list_directory(boot);

    bool has_versioned_kernel = false;
    for (auto const& name : names) {
        if (name.starts_with("vmlinuz"sv) && name != "vmlinuz"sv)
            has_versioned_kernel = true;
    }

    for (auto const& name : names) {
        if (!name.starts_with("vmlinuz"sv))
            continue;
        // Prefer vmlinuz-<version> over a plain vmlinuz symlink when both exist.
        if (name == "vmlinuz"sv && has_versioned_kernel)
            continue;

        KernelEntry entry;
        if (name == "vmlinuz"sv) {
            entry.version = kernel_version_from_modules(target);
        } else {
            entry.version = name.substring(7); // strip "vmlinuz"
            if (entry.version.starts_with('-'))
                entry.version = entry.version.substring(1);
        }
        entry.kernel = ByteString::formatted("/boot/{}", name);

        auto set_initrd = [&](ByteString const& candidate) {
            if (!entry.initrd.is_empty())
                return;
            if (access(ByteString::formatted("{}{}", target, candidate).characters(), R_OK) == 0)
                entry.initrd = candidate;
        };
        if (!entry.version.is_empty()) {
            set_initrd(ByteString::formatted("/boot/initrd.img-{}", entry.version));
            set_initrd(ByteString::formatted("/boot/initrd-{}", entry.version));
        }
        set_initrd("/boot/initrd.img"sv);
        set_initrd("/boot/initrd"sv);
        kernels.append(move(entry));
    }
    return kernels;
}

ByteString describe_plan(Vector<PlannedPartition> const& plan, bool uefi)
{
    StringBuilder builder;
    builder.append(uefi ? "UEFI layout\n"sv : "BIOS layout\n"sv);
    for (auto const& partition : plan) {
        if (partition.bios_boot)
            builder.appendff("  {}  {}  (BIOS boot)\n", partition.path, human_readable_size(partition.size_bytes).to_byte_string());
        else
            builder.appendff("  {}  {}  {}  {}\n", partition.path, human_readable_size(partition.size_bytes).to_byte_string(), filesystem_display_name(partition.fs_type), partition.mountpoint);
    }
    return builder.to_byte_string();
}

class Runner {
public:
    Runner(InstallConfig config, InstallCallbacks callbacks)
        : m_config(move(config))
        , m_callbacks(move(callbacks))
    {
    }

    ~Runner()
    {
        cleanup_mounts();
    }

    InstallResult execute()
    {
        if (geteuid() != 0) {
            m_error = "Halcyon Setup must run as root."sv;
            return { false, m_error };
        }

        if (!preflight())
            return finish();
        if (m_failed)
            return finish();

        if (!prepare_disk())
            return finish();
        if (m_failed)
            return finish();

        if (!create_filesystems())
            return finish();
        if (m_failed)
            return finish();

        if (!mount_targets())
            return finish();
        if (m_failed)
            return finish();

        if (!install_base())
            return finish();
        if (m_failed)
            return finish();

        if (!configure_repositories())
            return finish();
        if (m_failed)
            return finish();

        if (!install_required_packages())
            return finish();
        if (m_failed)
            return finish();

        if (!copy_live_boot_files())
            return finish();
        if (m_failed)
            return finish();

        if (!configure_desktop())
            return finish();
        if (m_failed)
            return finish();

        if (!configure_system())
            return finish();
        if (m_failed)
            return finish();

        if (!regenerate_shells())
            return finish();
        if (m_failed)
            return finish();

        if (!create_initramfs())
            return finish();
        if (m_failed)
            return finish();

        if (!install_bootloader())
            return finish();
        if (m_failed)
            return finish();

        advance(99, "Finishing up"sv);
        cleanup_mounts();
        advance(100, "Installation complete"sv);
        return { true, "Halcyon Linux has been installed."sv };
    }

private:
    InstallResult finish()
    {
        cleanup_mounts();
        if (m_error.is_empty())
            m_error = "The installation did not complete."sv;
        return { false, m_error };
    }

    bool cancelled()
    {
        if (m_cancelled)
            return true;
        if (m_callbacks.is_cancelled && m_callbacks.is_cancelled()) {
            m_cancelled = true;
            m_error = "The installation was cancelled."sv;
            return true;
        }
        return false;
    }

    void log(ByteString const& line)
    {
        if (m_callbacks.on_log)
            m_callbacks.on_log(line);
    }

    void advance(int percent, ByteString const& status)
    {
        m_percent = percent;
        log(ByteString::formatted("==> {}", status));
        if (m_callbacks.on_progress)
            m_callbacks.on_progress(percent, status);
    }

    bool run(Vector<ByteString> arguments, StringView description, ByteString const& input = {}, int timeout_seconds = 0, bool allow_failure = false)
    {
        if (cancelled())
            return false;

        log(ByteString::formatted("$ {}", join_arguments(arguments)));
        Vector<ByteString> command_arguments;
        for (size_t i = 1; i < arguments.size(); ++i)
            command_arguments.append(arguments[i]);
        auto result = run_command(arguments[0], command_arguments, input, timeout_seconds);

        auto output = result.combined_output().trim_whitespace();
        if (!output.is_empty())
            log(ByteString { output });

        if (!result.ok()) {
            auto message = ByteString::formatted("{} failed (exit code {}).", description, result.exit_code);
            if (allow_failure) {
                log(ByteString::formatted("warning: {}", message));
                return true;
            }
            m_failed = true;
            m_error = message;
            return false;
        }
        return true;
    }

    bool preflight()
    {
        advance(1, "Checking the installation environment"sv);

        for (auto const& program : { "lsblk"sv, "sfdisk"sv, "wipefs"sv, "partprobe"sv, "udevadm"sv, "mount"sv, "umount"sv, "chown"sv, "chmod"sv, "chimera-bootstrap"sv, "chimera-chroot"sv, "chpasswd"sv, "useradd"sv, "usermod"sv, "genfstab"sv, "blkid"sv }) {
            if (!command_exists(program)) {
                m_failed = true;
                m_error = ByteString::formatted("Required program '{}' was not found.", program);
                return false;
            }
        }
        if (!command_exists("mkfs.fat"sv) && !command_exists("mkfs.vfat"sv)) {
            m_failed = true;
            m_error = "Required program 'mkfs.fat' (dosfstools) was not found."sv;
            return false;
        }
        if (!command_exists("bcachefs"sv) && !command_exists("mkfs.bcachefs"sv)) {
            m_failed = true;
            m_error = "Formatting bcachefs requires bcachefs-tools, but it was not found."sv;
            return false;
        }

        auto probe = probe_devices();
        DiskInfo const* disk = nullptr;
        for (auto const& candidate : probe.disks) {
            if (candidate.path == m_config.target_device) {
                disk = &candidate;
                break;
            }
        }
        if (!disk) {
            m_failed = true;
            m_error = "The selected device is no longer present."sv;
            return false;
        }
        m_disk = *disk;

        for (auto const& mount : m_config.mounts) {
            if (device_is_mounted(mount.partition)) {
                m_failed = true;
                m_error = ByteString::formatted("{} is mounted. Unmount it in Partition Editor before installing.", mount.partition);
                return false;
            }
        }
        if (m_config.layout == LayoutMode::EntireDevice) {
            for (auto const& partition : m_disk.partitions) {
                if (device_is_mounted(partition.path)) {
                    m_failed = true;
                    m_error = ByteString::formatted("{} is mounted. Unmount it before installing over the whole device.", partition.path);
                    return false;
                }
            }
        }

        if (path_is_mounted(m_target)) {
            run({ "umount"sv, "-R"sv, "-f"sv, m_target }, "Unmounting the previous target"sv, {}, 30, true);
        }

        m_plan = whole_device_plan(m_disk, m_config.uefi);
        if (m_config.layout == LayoutMode::Custom)
            m_plan.partitions.clear();
        else if (!m_plan.error.is_empty()) {
            m_failed = true;
            m_error = m_plan.error;
            return false;
        }

        return true;
    }

    bool prepare_disk()
    {
        if (m_config.layout == LayoutMode::Custom) {
            advance(8, "Using the existing partition table"sv);
            return true;
        }

        advance(3, "Wiping the target device"sv);
        if (!run({ "wipefs"sv, "-a"sv, m_config.target_device }, "Wiping filesystem signatures"sv, {}, 30, true))
            return false;

        auto script = build_sfdisk_script();
        log(ByteString::formatted("Partitioning {}:\n{}", m_config.target_device, script));

        if (!run({ "sfdisk"sv, "--wipe"sv, "always"sv, "--wipe-partitions"sv, "always"sv, "--force"sv, m_config.target_device }, "Writing the partition table"sv, script, 30))
            return false;

        if (!run({ "partprobe"sv, m_config.target_device }, "Re-reading the partition table"sv, {}, 30, true))
            return false;
        run({ "udevadm"sv, "settle"sv }, "Waiting for the kernel"sv, {}, 30, true);

        for (auto const& partition : m_plan.partitions) {
            if (!wait_for_device(partition.path)) {
                m_failed = true;
                m_error = ByteString::formatted("Partition {} did not appear.", partition.path);
                return false;
            }
        }

        advance(12, "Partition table written"sv);
        return true;
    }

    ByteString build_sfdisk_script() const
    {
        StringBuilder builder;
        builder.append("label: gpt\n"sv);
        for (auto const& partition : m_plan.partitions) {
            auto start_mib = partition.start_bytes / (1024ULL * 1024ULL);
            if (partition.bios_boot) {
                builder.appendff("start={}MiB, size=1MiB, type=21686148-6449-6E6F-744E-656564454649, name=\"BIOS boot\"\n", start_mib);
                continue;
            }

            auto type = partition.esp
                ? "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"sv
                : "0FC63DAF-8483-4772-8E79-3D69D8477DE4"sv;
            if (partition.mountpoint == "/"sv)
                builder.appendff("start={}MiB, type={}, name=\"{}\"\n", start_mib, type, partition.label);
            else
                builder.appendff("start={}MiB, size={}MiB, type={}, name=\"{}\"\n", start_mib, partition.size_bytes / (1024ULL * 1024ULL), type, partition.label);
        }
        return builder.to_byte_string();
    }

    bool wait_for_device(ByteString const& path)
    {
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (access(path.characters(), F_OK) == 0)
                return true;
            usleep(50 * 1000);
        }
        return false;
    }

    bool create_filesystems()
    {
        advance(15, "Creating filesystems"sv);

        if (m_config.layout == LayoutMode::EntireDevice) {
            for (auto const& partition : m_plan.partitions) {
                if (partition.bios_boot)
                    continue;
                if (!format_partition(partition.path, partition.fs_type, partition.label, true))
                    return false;
            }
        } else {
            for (auto const& mount : m_config.mounts) {
                if (!mount.format)
                    continue;
                auto fs = mount.fs_type;
                if (mount.mountpoint == "swap"sv)
                    fs = "swap"sv;
                if (fs.is_empty()) {
                    m_failed = true;
                    m_error = ByteString::formatted("{} has no filesystem to create.", mount.partition);
                    return false;
                }
                if (!format_partition(mount.partition, fs, mount.label, true))
                    return false;
            }
        }

        advance(24, "Filesystems created"sv);
        return true;
    }

    bool format_partition(ByteString const& path, ByteString const& fs_type, ByteString const& label, bool required)
    {
        Vector<ByteString> arguments;
        auto program = ByteString {};

        if (filesystem_is_fat(fs_type)) {
            // dosfstools provides mkfs.fat; some images only expose the
            // mkfs.vfat compatibility name. Prefer the real binary.
            program = command_exists("mkfs.fat"sv) ? "mkfs.fat"sv : "mkfs.vfat"sv;
            arguments.append("-F"sv);
            arguments.append(fs_type == "fat16"sv ? "16"sv : "32"sv);
            if (!label.is_empty()) {
                arguments.append("-n"sv);
                arguments.append(label);
            }
        } else if (fs_type == "ext4"sv || fs_type == "ext3"sv || fs_type == "ext2"sv) {
            program = ByteString::formatted("mkfs.{}", fs_type);
            arguments.append("-F"sv);
            if (!label.is_empty()) {
                arguments.append("-L"sv);
                arguments.append(label);
            }
        } else if (fs_type == "bcachefs"sv) {
            if (command_exists("bcachefs"sv)) {
                program = "bcachefs"sv;
                arguments.append("format"sv);
            } else if (command_exists("mkfs.bcachefs"sv)) {
                program = "mkfs.bcachefs"sv;
            } else {
                m_failed = true;
                m_error = "Formatting bcachefs requires bcachefs-tools."sv;
                return false;
            }
            arguments.append("--force"sv);
            if (!label.is_empty())
                arguments.append(ByteString::formatted("--label={}", label));
        } else if (fs_type == "btrfs"sv) {
            program = "mkfs.btrfs"sv;
            arguments.append("-f"sv);
            if (!label.is_empty()) {
                arguments.append("-L"sv);
                arguments.append(label);
            }
        } else if (fs_type == "xfs"sv) {
            program = "mkfs.xfs"sv;
            arguments.append("-f"sv);
            if (!label.is_empty()) {
                arguments.append("-L"sv);
                arguments.append(label);
            }
        } else if (fs_type == "f2fs"sv) {
            program = "mkfs.f2fs"sv;
            arguments.append("-f"sv);
            if (!label.is_empty()) {
                arguments.append("-l"sv);
                arguments.append(label);
            }
        } else if (fs_type == "exfat"sv) {
            program = "mkfs.exfat"sv;
            if (!label.is_empty()) {
                arguments.append("-L"sv);
                arguments.append(label);
            }
        } else if (fs_type == "ntfs"sv) {
            program = "mkfs.ntfs"sv;
            arguments.append("-F"sv);
            if (!label.is_empty()) {
                arguments.append("-L"sv);
                arguments.append(label);
            }
        } else if (fs_type == "swap"sv || fs_type == "linux-swap"sv) {
            program = "mkswap"sv;
            if (!label.is_empty()) {
                arguments.append("-L"sv);
                arguments.append(label);
            }
        } else {
            if (!required)
                return true;
            m_failed = true;
            m_error = ByteString::formatted("Unsupported filesystem '{}'.", fs_type);
            return false;
        }

        if (!command_exists(program)) {
            m_failed = true;
            m_error = ByteString::formatted("Required program '{}' was not found.", program);
            return false;
        }

        // run() expects the program name as argument 0; the branches above
        // built only the flags and the device path.
        arguments.insert(0, program);
        arguments.append(path);
        return run(arguments, ByteString::formatted("Formatting {}", path));
    }

    bool mount_targets()
    {
        advance(25, "Mounting the target filesystem"sv);

        if (!make_path(m_target)) {
            m_failed = true;
            m_error = ByteString::formatted("Could not create {}.", m_target);
            return false;
        }
        if (path_is_mounted(m_target))
            run({ "umount"sv, "-R"sv, "-f"sv, m_target }, "Unmounting the target"sv, {}, 30, true);

        // Root must be mounted first. The remaining mounts are ordered by
        // depth so that /boot is mounted before /boot/efi.
        Vector<MountAssignment> ordered;
        for (auto const& mount : m_config.mounts) {
            if (mount.mountpoint == "/"sv)
                ordered.append(mount);
        }
        for (size_t depth = 1; depth < 8; ++depth) {
            for (auto const& mount : m_config.mounts) {
                if (mount.mountpoint == "/"sv || mount.mountpoint == "swap"sv)
                    continue;
                auto slashes = 0;
                for (auto character : mount.mountpoint) {
                    if (character == '/')
                        ++slashes;
                }
                if (static_cast<size_t>(slashes) == depth)
                    ordered.append(mount);
            }
        }

        for (auto const& mount : ordered) {
            auto destination = mount_target(m_target, mount.mountpoint);
            if (!make_path(destination)) {
                m_failed = true;
                m_error = ByteString::formatted("Could not create {}.", destination);
                return false;
            }
            if (!run({ "mount"sv, "-t"sv, mount.fs_type, mount.partition, destination }, ByteString::formatted("Mounting {}", mount.partition)))
                return false;
            m_mounted.append(destination);
        }

        chmod(m_target.characters(), 0755);

        // The upstream chimera-chroot mounts these over the target without
        // creating them. Make the mount points now; the live overlay has the
        // same workaround, but the installer must work without it too.
        for (auto directory : { "dev"sv, "proc"sv, "sys"sv, "tmp"sv })
            make_path(ByteString::formatted("{}/{}", m_target, directory));

        for (auto const& mount : m_config.mounts) {
            if (mount.mountpoint != "swap"sv)
                continue;
            if (!run({ "swapon"sv, mount.partition }, ByteString::formatted("Enabling swap on {}", mount.partition), {}, 30, true))
                return false;
            m_swaps.append(mount.partition);
        }

        advance(28, "Target mounted"sv);
        return true;
    }

    void ensure_halcyon_packages()
    {
        if (machine_architecture() != "x86_64"sv)
            return;

        // Keep the running Halcyon kernel and its out-of-tree modules in the
        // world file before base-live is removed. The live image already has
        // these; apk add makes them explicit in the installed system.
        Vector<ByteString> keep = {
            "chimera-chroot"sv, m_target, "apk"sv, "add"sv,
            "linux-halcyon"sv,
            "linux-halcyon-bcachefs-bin"sv,
            "linux-halcyon-broadcom-wl-bin"sv,
            "firmware-apple"sv,
        };
        run(move(keep), "Keeping the Halcyon kernel and Apple T2 firmware"sv, {}, 600, true);

        if (m_config.enable_t2fanrd)
            run({ "chimera-chroot"sv, m_target, "apk"sv, "add"sv, "t2fanrd"sv }, "Keeping T2 fan control"sv, {}, 300, true);
        else
            run({ "chimera-chroot"sv, m_target, "apk"sv, "del"sv, "t2fanrd"sv }, "Removing T2 fan control"sv, {}, 300, true);

        if (m_config.enable_tiny_dfr)
            run({ "chimera-chroot"sv, m_target, "apk"sv, "add"sv, "tiny-dfr"sv }, "Keeping Touch Bar support"sv, {}, 300, true);
        else
            run({ "chimera-chroot"sv, m_target, "apk"sv, "del"sv, "tiny-dfr"sv }, "Removing Touch Bar support"sv, {}, 300, true);
    }

    void ensure_bootloader_packages_in_world()
    {
        // base-live pulls in the offline bootloaders. Keep the selected one in
        // the world file before removing the live metapackage so that apk does
        // not collect it as an orphan.
        if (m_config.bootloader == Bootloader::Limine) {
            run({ "chimera-chroot"sv, m_target, "apk"sv, "add"sv, "limine"sv }, "Keeping Limine installed"sv, {}, 300, true);
            return;
        }

        auto arch = machine_architecture();
        auto kernel_arch = (arch == "x86_64"sv || arch == "amd64"sv) ? ByteString { "x86_64" } : arch;
        auto efi_arch = kernel_arch == "aarch64"sv ? ByteString { "arm64" } : kernel_arch;

        run({ "chimera-chroot"sv, m_target, "apk"sv, "add"sv, "grub"sv }, "Keeping GRUB installed"sv, {}, 300, true);
        if (m_config.uefi) {
            auto package = ByteString::formatted("grub-{}-efi", efi_arch);
            run({ "chimera-chroot"sv, m_target, "apk"sv, "add"sv, package }, "Keeping the GRUB EFI package installed"sv, {}, 300, true);
        } else {
            run({ "chimera-chroot"sv, m_target, "apk"sv, "add"sv, "grub-i386-pc"sv }, "Keeping the GRUB BIOS package installed"sv, {}, 300, true);
        }
    }

    bool install_base()
    {
        advance(30, "Installing the Halcyon base system"sv);
        if (!run({ "chimera-bootstrap"sv, "-l"sv, "-f"sv, m_target }, "Installing the live system"sv, {}, 3600))
            return false;

        // The live root filesystem is built with /, /etc and /usr owned by the
        // live build user. chimera-bootstrap preserves that ownership, which
        // makes systemd-tmpfiles refuse to create /run/dbus at boot; dbus then
        // fails and the desktop session cannot start. Restore root ownership.
        run({ "chown"sv, "0:0"sv, m_target }, "Fixing the ownership of the target root"sv, {}, 60);
        run({ "chown"sv, "-R"sv, "0:0"sv,
                ByteString::formatted("{}/etc", m_target),
                ByteString::formatted("{}/usr", m_target) },
            "Fixing the ownership of /etc and /usr"sv, {}, 600);

        // The live image ships ping with a file capability (cap_net_raw+p).
        // File capabilities are not preserved when the live filesystem is
        // copied onto the target, so set the traditional setuid bit instead.
        auto ping = ByteString::formatted("{}/usr/bin/ping", m_target);
        if (access(ping.characters(), F_OK) == 0)
            chmod(ping.characters(), 04755);

        ensure_halcyon_packages();

        if (m_config.remove_base_live) {
            advance(52, "Removing live-session packages"sv);
            ensure_bootloader_packages_in_world();
            run({ "chimera-chroot"sv, m_target, "apk"sv, "del"sv, "base-live"sv }, "Removing base-live"sv, {}, 300, true);
        }

        if (m_config.update_system) {
            advance(54, "Updating the installed system"sv);
            auto updated = run({ "chimera-chroot"sv, m_target, "apk"sv, "update"sv }, "Updating package indexes"sv, {}, 300, true);
            if (updated)
                run({ "chimera-chroot"sv, m_target, "apk"sv, "upgrade"sv, "--available"sv }, "Upgrading installed packages"sv, {}, 900, true);
        }

        return true;
    }

    bool configure_repositories()
    {
        advance(53, "Configuring the package repositories"sv);

        auto dir = ByteString::formatted("{}/etc/apk/repositories.d", m_target);
        if (!make_path(dir)) {
            m_failed = true;
            m_error = "Could not create the target apk repositories directory."sv;
            return false;
        }

        auto chimera = "v3 https://repo.chimera-linux.org/current/main\n"
                       "v3 https://repo.chimera-linux.org/current/user\n"sv;

        if (!write_file(ByteString::formatted("{}/00-chimera.list", dir), chimera)) {
            m_failed = true;
            m_error = "Could not write the target Chimera repositories."sv;
            return false;
        }

        // The Halcyon repository configuration is package-managed. The live
        // image carries both packages; installing them here also puts them in
        // the target world so they survive upgrades.
        if (!run({ "chimera-chroot"sv, m_target, "apk"sv, "add"sv,
                     "halcyon-repo-core"sv, "halcyon-repo-extra"sv },
                "Installing Halcyon repository packages"sv, {}, 300))
            return false;

        // Remove the pre-package overlay file if coming from an older image.
        unlink(ByteString::formatted("{}/20-halcyon.list", dir).characters());

        for (auto const* name : { "20-halcyon-core.list", "21-halcyon-extra.list" }) {
            auto path = ByteString::formatted("{}/{}", dir, name);
            if (access(path.characters(), F_OK) != 0) {
                m_failed = true;
                m_error = ByteString::formatted("The target is missing the {} repository file.", name);
                return false;
            }
        }

        // apk needs a trusted key for the Halcyon repositories. The live
        // image installs the key and chimera-bootstrap -l copies it, but fail
        // clearly here rather than leaving an unusable target.
        auto keydir = ByteString::formatted("{}/etc/apk/keys", m_target);
        bool found_key = false;
        for (auto const& name : list_directory(keydir)) {
            if (name.ends_with(".pub"sv)) {
                found_key = true;
                break;
            }
        }
        if (!found_key) {
            m_failed = true;
            m_error = "The target has no apk signing keys; the live image is missing the Halcyon key."sv;
            return false;
        }

        return true;
    }

    bool install_required_packages()
    {
        advance(55, "Installing required system tools"sv);
        auto arch = machine_architecture();
        auto is_x86_64 = arch == "x86_64"sv || arch == "amd64"sv;

        // The live image ships prebuilt modules for its own session. The
        // installed target builds them from the CKMS sources so they match
        // its kernel and are regenerated together with the initramfs.
        run({ "chimera-chroot"sv, m_target, "apk"sv, "del"sv,
                "linux-halcyon-bcachefs-bin"sv },
            "Removing the precompiled bcachefs module"sv, {}, 300, true);
        if (is_x86_64) {
            run({ "chimera-chroot"sv, m_target, "apk"sv, "del"sv,
                    "linux-halcyon-broadcom-wl-bin"sv },
                "Removing the precompiled Broadcom module"sv, {}, 300, true);
        }

        // The live image may already have CKMS-built modules at the same
        // paths. Removing the precompiled packages deleted those files, so
        // drop the module state and let the initramfs step rebuild them.
        run({ "chimera-chroot"sv, m_target, "rm"sv, "-rf"sv,
                "/var/lib/ckms/bcachefs"sv, "/var/lib/ckms/broadcom-wl"sv },
            "Resetting the CKMS module state"sv, {}, 60, true);

        // The kernel and its headers come from the live image, which was
        // built against the same package set. Check them rather than asking
        // apk to resolve them from a repository that may not have been
        // published yet.
        if (is_x86_64) {
            if (!run({ "chimera-chroot"sv, m_target, "apk"sv, "info"sv, "-e"sv, "linux-halcyon"sv },
                    "Checking the Halcyon kernel"sv, {}, 60))
                return false;
        } else {
            if (!run({ "chimera-chroot"sv, m_target, "apk"sv, "info"sv, "-e"sv, "linux-stable"sv },
                    "Checking the kernel"sv, {}, 60))
                return false;
        }

        Vector<ByteString> packages = {
            "uutils-coreutils"sv,
            "bcachefs-tools"sv,
            "ckms"sv,
            "clang"sv,
            "lld"sv,
            "gmake"sv,
            "rust"sv,
            "rust-bindgen"sv,
            "rust-src"sv,
            "python"sv,
            "kmod"sv,
            "initramfs-tools"sv,
            "git"sv,
            "bcachefs-ckms"sv,
            "base-shells"sv,
        };
        if (is_x86_64) {
            packages.append(ByteString { "broadcom-wl" });
            packages.append(ByteString { "broadcom-wl-ckms" });
            packages.append(ByteString { "linux-halcyon-devel" });
        } else {
            packages.append(ByteString { "linux-stable-devel" });
        }

        Vector<ByteString> arguments = {
            "chimera-chroot"sv,
            m_target,
            "apk"sv,
            "add"sv,
        };
        for (auto const& package : packages)
            arguments.append(package);

        return run(move(arguments),
            "Installing uutils, bcachefs tools, ckms, the kernel and kernel module sources"sv,
            {}, 900);
    }

    bool copy_live_boot_files()
    {
        advance(56, "Copying the live kernel into the target /boot"sv);
        auto target_boot = ByteString::formatted("{}/boot", m_target);
        make_path(target_boot);
        for (auto const& name : list_directory("/boot"sv)) {
            if (!name.starts_with("vmlinuz"sv) && !name.starts_with("config-"sv))
                continue;
            auto destination = ByteString::formatted("{}/{}", target_boot, name);
            if (access(destination.characters(), F_OK) == 0)
                continue;
            if (!copy_file(ByteString::formatted("/boot/{}", name), destination)) {
                m_failed = true;
                m_error = ByteString::formatted("Could not copy /boot/{} into the target.", name);
                return false;
            }
            log(ByteString::formatted("Copied /boot/{} into the target", name));
        }
        return true;
    }

    bool configure_desktop()
    {
        advance(57, "Configuring the desktop environment"sv);
        if (m_config.install_desktop) {
            if (!run({ "chimera-chroot"sv, m_target, "apk"sv, "add"sv,
                           "serenity-desktop"sv, "sddm"sv, "sddm-default-cage"sv, "sddm-theme-none"sv },
                    "Installing the Serenity Desktop Environment"sv, {}, 900))
                return false;

            // sddm-theme-none means there is no greeter theme, so the greeter
            // exits immediately. Log the first user in automatically and start
            // the SDE session, matching the live image.
            enable_service("sddm"sv);
            if (!m_config.users.is_empty()) {
                auto config_dir = ByteString::formatted("{}/etc/sddm.conf.d", m_target);
                make_path(config_dir);
                auto autologin = ByteString::formatted(
                    "[Autologin]\n"
                    "User={}\n"
                    "Session=sde\n",
                    m_config.users[0].username);
                if (!write_file(ByteString::formatted("{}/autologin.conf", config_dir), autologin)) {
                    m_failed = true;
                    m_error = "Could not configure automatic login for the desktop."sv;
                    return false;
                }
                log(ByteString::formatted("Configured automatic login for {}", m_config.users[0].username));
            }
            return true;
        }

        // Keep the required tools above, but leave a console-only system.
        run({ "chimera-chroot"sv, m_target, "apk"sv, "del"sv,
                "serenity-desktop"sv, "serenity-lagom"sv, "sddm"sv, "sddm-default-cage"sv,
                "sddm-theme-none"sv, "ladybird-sde"sv },
            "Removing the Serenity Desktop Environment"sv, {}, 600, true);
        return true;
    }

    ByteString target_path(StringView path) const
    {
        if (path == "/"sv)
            return m_target;
        return ByteString::formatted("{}{}", m_target, path);
    }

    ByteString root_mount_fs() const
    {
        for (auto const& mount : m_config.mounts) {
            if (mount.mountpoint == "/"sv)
                return mount.fs_type;
        }
        return {};
    }

    ByteString boot_mount_fs() const
    {
        for (auto const& mount : m_config.mounts) {
            if (mount.mountpoint == "/boot"sv)
                return mount.fs_type;
        }
        return {};
    }

    ByteString root_partition() const
    {
        for (auto const& mount : m_config.mounts) {
            if (mount.mountpoint == "/"sv)
                return mount.partition;
        }
        return {};
    }

    bool configure_system()
    {
        advance(58, "Configuring the installed system"sv);

        auto hostname_path = ByteString::formatted("{}/etc/hostname", m_target);
        write_file(hostname_path, ByteString::formatted("{}\n", m_config.hostname));

        auto hosts = ByteString::formatted(
            "127.0.0.1 localhost\n"
            "::1 localhost\n"
            "127.0.1.1 {}\n",
            m_config.hostname);
        write_file(ByteString::formatted("{}/etc/hosts", m_target), hosts);

        auto zoneinfo = ByteString::formatted("{}/usr/share/zoneinfo/{}", m_target, m_config.timezone);
        auto localtime = ByteString::formatted("{}/etc/localtime", m_target);
        if (access(zoneinfo.characters(), R_OK) == 0) {
            replace_symlink(ByteString::formatted("/usr/share/zoneinfo/{}", m_config.timezone), localtime);
        } else {
            log(ByteString::formatted("warning: time zone {} was not found; using UTC", m_config.timezone));
            replace_symlink("/usr/share/zoneinfo/UTC"sv, localtime);
        }

        write_file(ByteString::formatted("{}/etc/hwclock", m_target),
            m_config.hardware_clock_localtime ? "localtime\n"sv : "utc\n"sv);

        auto keyboard = ByteString::formatted(
            "XKBMODEL=\"pc105\"\n"
            "XKBLAYOUT=\"{}\"\n"
            "XKBVARIANT=\"{}\"\n"
            "XKBOPTIONS=\"\"\n"
            "BACKSPACE=\"guess\"\n",
            m_config.keyboard_layout,
            m_config.keyboard_variant);
        write_file(ByteString::formatted("{}/etc/default/keyboard", m_target), keyboard);

        if (root_mount_fs() == "bcachefs"sv)
            append_modules_if_missing(m_target, "bcachefs"sv);

        advance(60, "Writing /etc/fstab"sv);
        auto fstab_result = run_command("genfstab"sv, { "-U"sv, m_target }, {}, 60);
        if (fstab_result.ok() && !fstab_result.output.is_empty()) {
            write_file(ByteString::formatted("{}/etc/fstab", m_target), fstab_result.output);
        } else {
            log("warning: genfstab failed; writing a minimal /etc/fstab"sv);
            if (!write_minimal_fstab())
                return false;
        }

        // The live session has desktop launchers for the setup wizard. Do not
        // copy them into the installed system's skeleton for new users.
        unlink(ByteString::formatted("{}/etc/skel/Desktop/HalcyonSetup.desktop", m_target).characters());
        unlink(ByteString::formatted("{}/etc/skel/Desktop/Install Halcyon Linux", m_target).characters());
        unlink(ByteString::formatted("{}/etc/skel/.config/autostart/HalcyonSetup.desktop", m_target).characters());
        // The live test image can carry an automatic root console (ttyS0) and
        // an automatic installer start. Never copy those into an installed
        // system.
        unlink(ByteString::formatted("{}/etc/dinit.d/boot.d/serial-getty", m_target).characters());
        unlink(ByteString::formatted("{}/etc/dinit.d/serial-getty", m_target).characters());
        unlink(ByteString::formatted("{}/etc/dinit.d/boot.d/halcyon-live-wizard", m_target).characters());
        unlink(ByteString::formatted("{}/etc/dinit.d/halcyon-live-wizard", m_target).characters());

        advance(64, "Creating user accounts"sv);
        for (auto const& user : m_config.users) {
            auto full_name = user.full_name.is_empty() ? user.username : user.full_name;
            if (!run({ "chimera-chroot"sv, m_target, "useradd"sv, "-m"sv, "-c"sv, full_name, "-s"sv, "/bin/sh"sv, user.username }, ByteString::formatted("Creating user {}", user.username)))
                return false;

            Vector<ByteString> groups;
            for (auto const& group : user.groups)
                groups.append(group);
            if (groups.is_empty())
                groups.append("wheel"sv);

            Vector<ByteString> arguments = { "chimera-chroot"sv, m_target, "usermod"sv, "-a"sv, "-G"sv };
            StringBuilder group_list;
            for (size_t i = 0; i < groups.size(); ++i) {
                if (i > 0)
                    group_list.append(',');
                group_list.append(groups[i]);
            }
            arguments.append(group_list.to_byte_string());
            arguments.append(user.username);
            run(arguments, ByteString::formatted("Adding {} to additional groups", user.username), {}, 60, true);

            // useradd copies the skeleton preserving its ownership. The live
            // skeleton is not owned by the new user, so make the home
            // directory belong to the account that owns it.
            run({ "chimera-chroot"sv, m_target, "chown"sv, "-R"sv,
                    ByteString::formatted("{}:{}", user.username, user.username),
                    ByteString::formatted("/home/{}", user.username) },
                ByteString::formatted("Fixing ownership of /home/{}", user.username), {}, 300, true);

            // Give every account the SDE 3D models in the directory the viewer
            // uses by default.
            auto models_source = ByteString { "/usr/lib/sde/share/res/3D Models"sv };
            if (access(models_source.characters(), R_OK) != 0)
                models_source = ByteString { "/usr/lib/sde/share/res/3DModels"sv };
            if (access(models_source.characters(), R_OK) == 0) {
                auto models_destination = ByteString::formatted("{}/home/{}/Documents/3DModels", m_target, user.username);
                make_path(models_destination);
                for (auto const& entry : list_directory(models_source)) {
                    auto source_path = ByteString::formatted("{}/{}", models_source, entry);
                    auto destination_path = ByteString::formatted("{}/{}", models_destination, entry);
                    copy_file(source_path, destination_path);
                }
                run({ "chimera-chroot"sv, m_target, "chown"sv, "-R"sv,
                        ByteString::formatted("{}:{}", user.username, user.username),
                        ByteString::formatted("/home/{}/Documents/3DModels", user.username) },
                    ByteString::formatted("Setting ownership of the 3D models for {}", user.username), {}, 60, true);
                log(ByteString::formatted("Copied the SDE 3D models for {}", user.username));
            } else {
                log("warning: no SDE 3D models were found; skipping the per-user copy"sv);
            }
        }

        advance(68, "Setting passwords"sv);
        StringBuilder password_input;
        password_input.appendff("root:{}\n", m_config.root_password);
        for (auto const& user : m_config.users)
            password_input.appendff("{}:{}\n", user.username, user.password);

        auto chpasswd = run_command("chimera-chroot"sv, { m_target, "chpasswd"sv, "-c"sv, "SHA512"sv }, password_input.to_byte_string(), 120);
        if (!chpasswd.ok()) {
            chpasswd = run_command("chimera-chroot"sv, { m_target, "chpasswd"sv }, password_input.to_byte_string(), 120);
            if (!chpasswd.ok()) {
                m_failed = true;
                m_error = "Could not set the root or user passwords."sv;
                return false;
            }
        }
        // chpasswd creates /etc/shadow with the process umask (0644). It has to
        // stay unreadable for everyone but root.
        chmod(ByteString::formatted("{}/etc/shadow", m_target).characters(), 0600);

        advance(71, "Enabling services"sv);
        if (m_config.enable_syslog)
            enable_service("syslog-ng"sv);
        if (m_config.enable_networkmanager)
            enable_service("networkmanager"sv);
        if (m_config.enable_ntp) {
            if (!enable_service("chrony"sv))
                enable_service("chronyd"sv);
        }

        restore_setuid_bits();
        return true;
    }

    bool write_minimal_fstab()
    {
        StringBuilder builder;
        for (auto const& mount : m_config.mounts) {
            auto uuid = root_uuid_for(mount.partition);
            if (uuid.is_empty()) {
                m_failed = true;
                m_error = ByteString::formatted("Could not determine the UUID of {}.", mount.partition);
                return false;
            }
            if (mount.mountpoint == "swap"sv) {
                builder.appendff("UUID={} none swap sw 0 0\n", uuid);
            } else if (mount.mountpoint == "/"sv) {
                builder.appendff("UUID={} / {} defaults 0 1\n", uuid, mount.fs_type);
            } else {
                builder.appendff("UUID={} {} {} defaults 0 2\n", uuid, mount.mountpoint, mount.fs_type);
            }
        }
        return write_file(ByteString::formatted("{}/etc/fstab", m_target), builder.to_byte_string());
    }

    bool enable_service(StringView name)
    {
        auto system_file = ByteString::formatted("{}/usr/lib/dinit.d/{}", m_target, name);
        auto local_file = ByteString::formatted("{}/etc/dinit.d/{}", m_target, name);
        if (access(system_file.characters(), F_OK) != 0 && access(local_file.characters(), F_OK) != 0)
            return false;

        auto boot_dir = ByteString::formatted("{}/etc/dinit.d/boot.d", m_target);
        make_path(boot_dir);
        auto link_path = ByteString::formatted("{}/{}", boot_dir, name);
        auto target = ByteString::formatted("../{}", name);
        if (replace_symlink(target, link_path)) {
            log(ByteString::formatted("Enabled service {}", name));
            return true;
        }
        return false;
    }

    void restore_setuid_bits()
    {
        // Package metadata marks these files setuid root. chown(2) clears the
        // setuid bit when it changes ownership, so after fixing the target's
        // ownership we have to put the bits back.
        static constexpr StringView programs[] = {
            "/usr/lib/polkit-1/polkit-agent-helper-1"sv,
            "/usr/bin/pkexec"sv,
            "/usr/bin/mount.nfs"sv,
            "/usr/bin/unix_chkpwd"sv,
            "/usr/bin/ksu"sv,
            "/usr/bin/crontab"sv,
            "/usr/bin/fusermount3"sv,
            "/usr/lib/ssh-keysign"sv,
            "/usr/lib/libgtop_server2"sv,
            "/usr/bin/mount"sv,
            "/usr/bin/umount"sv,
            "/usr/lib/qemu-bridge-helper"sv,
            "/usr/lib/dbus-daemon-launch-helper"sv,
            "/usr/bin/doas"sv,
            "/usr/bin/chage"sv,
            "/usr/bin/chfn"sv,
            "/usr/bin/chsh"sv,
            "/usr/bin/expiry"sv,
            "/usr/bin/gpasswd"sv,
            "/usr/bin/newgidmap"sv,
            "/usr/bin/newuidmap"sv,
            "/usr/bin/newgrp"sv,
            "/usr/bin/passwd"sv,
            "/usr/bin/sg"sv,
            "/usr/bin/su"sv,
            "/usr/lib/chromium/chrome-sandbox"sv,
            "/usr/lib/Xorg.wrap"sv,
            "/usr/bin/firejail"sv,
            "/usr/bin/wlock"sv,
            "/usr/lib/lxc/lxc-user-nic"sv,
        };
        for (auto const& program : programs) {
            auto path = ByteString::formatted("{}{}", m_target, program);
            if (access(path.characters(), F_OK) == 0)
                chmod(path.characters(), 04755);
        }
        log("Restored setuid permissions"sv);
    }

    bool regenerate_shells()
    {
        advance(73, "Regenerating /etc/shells"sv);

        // The live overlay provides this helper. Older images may already
        // have a valid /etc/shells, so a missing helper is only a warning;
        // the check below is authoritative.
        run({ "chimera-chroot"sv, m_target, "/usr/local/bin/halcyon-regenerate-shells"sv },
            "Regenerating /etc/shells"sv, {}, 60, true);

        if (!run({ "chimera-chroot"sv, m_target, "grep"sv, "-qx"sv, "/bin/sh"sv, "/etc/shells"sv },
                "Checking /etc/shells"sv, {}, 30))
            return false;

        return true;
    }

    bool create_initramfs()
    {
        advance(74, "Building kernel modules and creating the initial ramdisk"sv);

        // CKMS builds the modules for every installed kernel that has headers
        // in /usr/lib/modules/<kern>/build. The helper defers initramfs
        // regeneration, so update-initramfs below picks up the new modules.
        if (!run({ "chimera-chroot"sv, m_target, "/usr/lib/kernel.d/10-ckms.sh"sv },
                "Building the bcachefs and Broadcom modules with CKMS"sv, {}, 3600))
            return false;

        if (!run({ "chimera-chroot"sv, m_target, "depmod"sv, "-a"sv },
                "Updating module dependencies"sv, {}, 300))
            return false;

        if (!run({ "chimera-chroot"sv, m_target, "update-initramfs"sv, "-c"sv, "-k"sv, "all"sv },
                "Creating initramfs images"sv, {}, 1200))
            return false;

        advance(82, "Initial ramdisk created"sv);
        return true;
    }

    bool install_bootloader()
    {
        advance(84, "Installing the bootloader"sv);
        if (m_config.bootloader == Bootloader::Limine)
            return install_limine();
        return install_grub();
    }

    bool install_limine()
    {
        auto kernels = find_kernels(m_target);
        if (kernels.is_empty()) {
            m_failed = true;
            m_error = "No kernel was found in /boot. The installed system cannot boot."sv;
            return false;
        }

        auto package_present = access(ByteString::formatted("{}/usr/bin/limine", m_target).characters(), X_OK) == 0
            || access(ByteString::formatted("{}/usr/sbin/limine", m_target).characters(), X_OK) == 0;
        if (!package_present) {
            run({ "chimera-chroot"sv, m_target, "apk"sv, "add"sv, "limine"sv }, "Installing Limine"sv, {}, 600, true);
            package_present = access(ByteString::formatted("{}/usr/bin/limine", m_target).characters(), X_OK) == 0
                || access(ByteString::formatted("{}/usr/sbin/limine", m_target).characters(), X_OK) == 0;
        }
        if (!package_present) {
            m_failed = true;
            m_error = "Limine is not installed in the target system."sv;
            return false;
        }

        if (m_config.uefi) {
            auto source_dir = ByteString::formatted("{}/usr/share/limine", m_target);
            auto destination_dir = ByteString::formatted("{}/boot/EFI/BOOT", m_target);
            make_path(destination_dir);
            bool copied = false;
            for (auto const& entry : list_directory(source_dir)) {
                if (!entry.ends_with(".EFI"sv))
                    continue;
                if (copy_file(ByteString::formatted("{}/{}", source_dir, entry), ByteString::formatted("{}/{}", destination_dir, entry))) {
                    copied = true;
                    log(ByteString::formatted("Installed EFI bootloader {}", entry));
                }
            }
            if (!copied) {
                m_failed = true;
                m_error = "Limine EFI binaries were not found in /usr/share/limine."sv;
                return false;
            }
        } else {
            // Limine's BIOS stage 1/2 loader looks for stage 3
            // (limine-bios.sys) in /boot, /boot/limine, /limine or /. The
            // package does not install it onto the target boot partition, so
            // copy it before bios-install. Without this the installed system
            // reports "Stage 3 file not found".
            auto source = ByteString::formatted("{}/usr/share/limine/limine-bios.sys", m_target);
            auto destination_dir = ByteString::formatted("{}/boot/limine", m_target);
            make_path(destination_dir);
            if (access(source.characters(), R_OK) != 0) {
                m_failed = true;
                m_error = "Limine BIOS stage 3 (limine-bios.sys) was not found in /usr/share/limine."sv;
                return false;
            }
            if (!copy_file(source, ByteString::formatted("{}/limine-bios.sys", destination_dir))) {
                m_failed = true;
                m_error = "Could not copy limine-bios.sys into /boot/limine."sv;
                return false;
            }
            log("Installed Limine BIOS stage 3"sv);
            if (!run({ "chimera-chroot"sv, m_target, "limine"sv, "bios-install"sv, m_config.target_device }, "Installing Limine to the disk"sv, {}, 120))
                return false;
        }

        auto root_uuid = root_uuid_for(root_partition());
        if (root_uuid.is_empty()) {
            m_failed = true;
            m_error = "Could not determine the UUID of the root filesystem."sv;
            return false;
        }

        StringBuilder config;
        config.append("timeout: 5\n"sv);
        config.append("default_entry: 1\n\n"sv);
        for (auto const& kernel : kernels) {
            config.appendff("/Halcyon Linux {}\n", kernel.version);
            config.append("    protocol: linux\n"sv);
            config.appendff("    kernel_path: boot():/{}\n", kernel.kernel.substring(6));
            if (!kernel.initrd.is_empty())
                config.appendff("    module_path: boot():/{}\n", kernel.initrd.substring(6));
            config.appendff("    cmdline: root=UUID={} rw quiet\n\n", root_uuid);
        }

        return write_file(ByteString::formatted("{}/boot/limine.conf", m_target), config.to_byte_string());
    }

    bool install_grub()
    {
        auto arch = machine_architecture();
        auto kernel_arch = (arch == "x86_64"sv || arch == "amd64"sv) ? ByteString { "x86_64" } : arch;
        auto efi_arch = kernel_arch;
        if (kernel_arch == "aarch64"sv)
            efi_arch = "arm64"sv;
        auto efi_package = ByteString::formatted("grub-{}-efi", efi_arch);
        auto bios_package = "grub-i386-pc"sv;

        if (m_config.uefi) {
            auto grub_install = ByteString::formatted("{}/usr/sbin/grub-install", m_target);
            if (access(grub_install.characters(), X_OK) != 0) {
                Vector<ByteString> install_efi = { "chimera-chroot"sv, m_target, "apk"sv, "add"sv };
                install_efi.append(efi_package);
                run(move(install_efi), "Installing GRUB EFI"sv, {}, 600, true);
            }

            ByteString efi_directory = "/boot"sv;
            for (auto const& mount : m_config.mounts) {
                if (mount.mountpoint == "/boot/efi"sv && filesystem_is_fat(mount.fs_type)) {
                    efi_directory = "/boot/efi"sv;
                    break;
                }
            }

            Vector<ByteString> grub_install_arguments = {
                "chimera-chroot"sv, m_target, "grub-install"sv,
                ByteString::formatted("--target={}-efi", efi_arch),
                ByteString::formatted("--efi-directory={}", efi_directory),
                "--boot-directory=/boot"sv, "--removable"sv,
            };
            if (!run(move(grub_install_arguments), "Installing GRUB"sv, {}, 300))
                return false;
        } else {
            auto grub_install = ByteString::formatted("{}/usr/sbin/grub-install", m_target);
            if (access(grub_install.characters(), X_OK) != 0) {
                run({ "chimera-chroot"sv, m_target, "apk"sv, "add"sv, bios_package }, "Installing GRUB for BIOS"sv, {}, 600, true);
            }
            if (!run({ "chimera-chroot"sv, m_target, "grub-install"sv, "--target=i386-pc"sv, "--boot-directory=/boot"sv, "--recheck"sv, m_config.target_device }, "Installing GRUB"sv, {}, 300))
                return false;
        }

        if (!run({ "chimera-chroot"sv, m_target, "update-grub"sv }, "Generating the GRUB configuration"sv, {}, 300))
            return false;

        advance(95, "Bootloader installed"sv);
        return true;
    }

    void cleanup_mounts()
    {
        if (m_cleaned_up)
            return;
        m_cleaned_up = true;

        sync();

        for (auto& swap : m_swaps)
            run_command("swapoff"sv, { swap }, {}, 30);

        for (size_t i = m_mounted.size(); i > 0; --i) {
            auto const& path = m_mounted[i - 1];
            auto result = run_command("umount"sv, { "-R"sv, "-f"sv, path }, {}, 30);
            if (!result.ok())
                run_command("umount"sv, { "-l"sv, path }, {}, 30);
        }
        m_mounted.clear();
        m_swaps.clear();
    }

    InstallConfig m_config;
    InstallCallbacks m_callbacks;
    ByteString m_target { "/mnt/halcyon"sv };
    DiskInfo m_disk;
    PlanResult m_plan;
    Vector<ByteString> m_mounted;
    Vector<ByteString> m_swaps;
    ByteString m_error;
    int m_percent { 0 };
    bool m_failed { false };
    bool m_cancelled { false };
    bool m_cleaned_up { false };
};

InstallResult run_install(InstallConfig const& config, InstallCallbacks callbacks)
{
    Runner runner { config, move(callbacks) };
    return runner.execute();
}

}
