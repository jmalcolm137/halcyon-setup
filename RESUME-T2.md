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

## Build progress (2026-11-10)

All packages are built in `bldroot-halcyon`:

- `linux-halcyon-7.3.1-r0` (plus `-devel`, `-dinit`)
- `linux-halcyon-bcachefs-bin-1.39.7.7.3.1-r0`
- `linux-halcyon-broadcom-wl-bin-6.30.223.271.7.3.1-r0`
- `broadcom-wl-6.30.223.271-r2`
- `halcyon-setup-0.1.0-r13`
- `firmware-apple`, `t2fanrd`, `tiny-dfr` (already present)

The first ISO (`halcyon-linux-x86_64-LIVE-20261110-sde.iso`) was built and
verified: `linux-halcyon`, both module packages, `firmware-apple`, `t2fanrd`,
`tiny-dfr`, `wifi-select`, the init-top script and the dinit service are all
present, and the initrd contains `wl.ko`, `brcmfmac.ko`, `brcmutil.ko`,
`000-halcyon-wifi` and `wifi-select`.

Booting that ISO in QEMU found dinit bugs: `type = oneshot` is not valid and
aborted the boot.

- `halcyon-core/broadcom-wl/files/halcyon-wifi` is now `type = scripted`
  (pkgrel 2, cports `6cb17362c`).
- `halcyon-core/linux-halcyon/files/zram` was the next one; also fixed to
  `type = scripted` (pkgrel 1, cports `be67ea564`). This requires a full kernel
  package rebuild, which is running now:
  `./cbuild -b bldroot-halcyon -D -j8 pkg halcyon-core/linux-halcyon`
  Log: `/home/jmalcolm/Projects/halcyon/kernel-build2.log`

## Gotchas found while building

- `prepare-upgrade` resets `pkgrel` to 0; set it back after refreshing checksums.
- `broadcom-wl` lint requires: `depends` before `pkgdesc`, `options` last with a
  comment directly above it, and `dinit-chimera` in `makedepends` for the
  `early-fs-local.target` service dependency.
- `linux-halcyon-bcachefs-bin` must not install a license; the ckms source tree
  has no `COPYING`.
- cbuild gzips everything under `/usr/share/man`; `mklive.sh` now decompresses
  `*.md.gz` so Serenity Help can read the manuals.

## Next commands

1. Rebuild the ISO after the dinit fix:

```
cd /home/jmalcolm/Projects/halcyon
./build-halcyon-iso.sh
```

The stale `build-halcyon` directory is root-owned; remove it first with:

```
docker run --rm -v /home/jmalcolm/Projects/halcyon:/h alpine \
    sh -c 'rm -rf /h/build-halcyon'
```

2. Boot the new ISO in QEMU and confirm the desktop starts and
   `/var/log/halcyon-wifi.log` shows a non-T2 selection (this machine has no
   T2, so it should blacklist `brcmfmac` and load `wl`, or skip both if the
   hardware is absent).

3. Publish the next ISO release and update `SDE-BUILD-STATE.md`.

## Notes

- T2 detection was unit-tested with a fake sysfs tree:
  - T2 initramfs config blacklists `wl` and defers `brcmfmac`.
  - T2 apply config removes the `brcmfmac` blacklist and runs
    `modprobe -r wl`, `modprobe brcmfmac`, `modprobe brcmfmac_wcc`.
  - Non-T2 config blacklists `brcmfmac` and runs `modprobe -r brcmfmac`,
    `modprobe wl`.
- `firmware-apple` contains the BCM4377b (`brcmfmac4377b3-pcie...`) firmware.
- The user's `tiny-dlr` is the cports package `tiny-dfr` (Touch Bar daemon).
  `t2fand` is the cports package `t2fanrd`.
