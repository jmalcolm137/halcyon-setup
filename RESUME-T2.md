# Resume: Apple T2 support (2026-11-09)

This file exists so work can resume after the power loss.

## Finished and pushed

- `halcyon-setup` (main):
  - `62ceaaf` tests: cover the Options page
  - `c6d248e` packaging: keep T2 daemons optional
  - `daceae5` tests: cover the required user account
  - `d57a9ab` Add Apple T2 kernel, Wi-Fi and option support
- `halcyon-installer` (master):
  - `1e00776` live: use linux-halcyon and add Apple T2 support packages
- `cports` (halcyon):
  - `aeb148e1f` halcyon: move to linux-halcyon and add runtime T2 Wi-Fi selection

## What is in place

- `linux-halcyon` moved from `halcyon-extra` to `halcyon-core`.
- Removed `halcyon-core/linux-stable-bcachefs-bin` and
  `halcyon-core/linux-stable-broadcom-wl-bin`.
- Added `halcyon-core/linux-halcyon-bcachefs-bin` and
  `halcyon-core/linux-halcyon-broadcom-wl-bin`.
- `broadcom-wl` no longer installs a hardcoded `brcmfmac` blacklist.
- Runtime Wi-Fi selection:
  - `/usr/lib/halcyon/wifi-select` detects T2 via PCI `106b:1801` with a
    DMI fallback.
  - initramfs `000-halcyon-wifi` writes `/run/modprobe.d/50-halcyon-wifi.conf`
    before udev.
  - dinit `halcyon-wifi` applies the selection after the real root is mounted
    and loads `brcmfmac` (T2) or `wl` (non-T2).
  - initramfs hook includes `wl`, `brcmfmac`, `brcmutil`, `brcmfmac_wcc`.
- Live image (`mklive-image.sh`) x86_64:
  - kernel `linux-halcyon`
  - `linux-halcyon-bcachefs-bin`
  - `linux-halcyon-broadcom-wl-bin`
  - `broadcom-wl`
  - `firmware-apple`
  - `t2fanrd`
  - `tiny-dfr`
  - no ZFS packages
- Setup Options page has `t2fanrd` and `tiny-dfr` checkboxes, both default on.
- Installer keeps `linux-halcyon`, the two modules and `firmware-apple` in
  the target, and adds/removes `t2fanrd` / `tiny-dfr` according to the Options
  checkboxes.
- Removed the local `build-overlay` HalcyonSetup.desktop; the package installs
  the executable `Install Halcyon Linux` launcher instead.
- `halcyon/initramfs-tools/hooks/broadcom-wl` now adds both driver families.

## Status: complete (2026-11-11)

All Apple T2 work is done and published.

- `cports` (halcyon): kernel, module, `broadcom-wl` and `halcyon-setup`
  packages built; dinit services use `type = scripted`.
- `halcyon-setup` (main): Options checkboxes plus the finished page
  **Eject and restart** button (`c46ccfb`).
- `halcyon-installer` (master): `linux-halcyon` live image with
  `firmware-apple`, `t2fanrd`, `tiny-dfr`; SDE manuals decompressed; NVIDIA
  firmware kept out of the initrd.
- Release `0.1.12` / `halcyon-linux-x86_64-LIVE-20261111-sde-r19.iso`
  (2,116,790,272 bytes,
  sha256 `43e038dcbbe018e3ef49c6444593a40b419c7e572f53b720ccbbc480b31addac`)
  is published from the `halcyon-installer` repository.

Verified: the ISO boots to SDDM with no dinit errors; the rootfs and initrd
contain both Wi-Fi drivers, the selector, the init-top script, the dinit
service and all T2 packages. Real T2 hardware was not available; the Wi-Fi
selection logic was unit-tested with a fake sysfs tree.

## Gotchas found while building

- `prepare-upgrade` resets `pkgrel` to 0; set it back after refreshing checksums.
- `broadcom-wl` lint requires: `depends` before `pkgdesc`, `options` last with a
  comment directly above it, and `dinit-chimera` in `makedepends` for the
  `early-fs-local.target` service dependency.
- `linux-halcyon-bcachefs-bin` must not install a license; the ckms source tree
  has no `COPYING`.
- dinit does not accept `type = oneshot`; use `type = scripted`.
- cbuild gzips everything under `/usr/share/man`; `mklive.sh` decompresses
  `*.md.gz` so Serenity Help can read the manuals.
- The stale `build-halcyon` directory is root-owned; remove it with
  `docker run --rm -v /home/jmalcolm/Projects/halcyon:/h alpine sh -c 'rm -rf /h/build-halcyon'`.
- GitHub rejects release assets of 2 GiB or more. The NVIDIA firmware
  (~100 MiB) is excluded from the initrd to stay under the limit.
