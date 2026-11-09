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

## Interrupted work

`linux-halcyon` was being built from its new `halcyon-core` location:

```
cd /home/jmalcolm/Projects/cports
./cbuild -b bldroot-halcyon -j4 pkg halcyon-core/linux-halcyon
```

Log: `/tmp/cbuild-linux-halcyon2.log`

This may need to be restarted after power loss. cbuild is incremental.

## Next commands

1. Rebuild `linux-halcyon` if needed:

```
cd /home/jmalcolm/Projects/cports
./cbuild -b bldroot-halcyon -j4 pkg halcyon-core/linux-halcyon
```

2. Build the new modules and modified broadcom-wl:

```
./cbuild -b bldroot-halcyon -j4 pkg halcyon-core/linux-halcyon-bcachefs-bin
./cbuild -b bldroot-halcyon -j4 pkg halcyon-core/broadcom-wl
./cbuild -b bldroot-halcyon -j4 pkg halcyon-core/linux-halcyon-broadcom-wl-bin
```

3. Build `halcyon-setup` from the new source commit:

```
./cbuild -b bldroot-halcyon -j4 pkg halcyon-core/halcyon-setup
```

The cports template currently points at
`daceae5dd7d38d32992559db327f50415f3451e3`; the latest source is `62ceaaf`,
but only tests/packaging changed after daceae5. Bump if desired.

4. Ensure `firmware-apple`, `t2fanrd`, `tiny-dfr` packages are present and
   indexed.

5. Rebuild the ISO:

```
cd /home/jmalcolm/Projects/halcyon
./build-halcyon-iso.sh
```

6. Verify the built rootfs:
   - `linux-halcyon` installed
   - `linux-halcyon-bcachefs-bin` and `linux-halcyon-broadcom-wl-bin`
   - `firmware-apple`, `t2fanrd`, `tiny-dfr`
   - `/usr/lib/halcyon/wifi-select`
   - `/usr/share/initramfs-tools/scripts/init-top/000-halcyon-wifi`
   - `/usr/lib/dinit.d/halcyon-wifi` and its boot.d link
   - `/run/modprobe.d` selection in a booted image

7. Publish the next ISO release and update `SDE-BUILD-STATE.md`.

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
