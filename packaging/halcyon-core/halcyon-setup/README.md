# halcyon-core/halcyon-setup

Copy this directory to the Halcyon cports checkout:

```
cp -a packaging/halcyon-core/halcyon-setup ../cports/halcyon-core/
```

The Halcyon cports branch layers `halcyon-core` on `halcyon-extra`
(`halcyon-core/.parent -> ../halcyon-extra`) so core packages can use
`serenity-lagom-devel` from the SDE repository. Keep that link in place.

Before building, replace `_commit` with the released commit and update
`sha256` with the digest of the GitHub archive:

```
curl -L -o /tmp/halcyon-setup.tar.gz \
    https://github.com/jmalcolm137/halcyon-setup/archive/<commit>.tar.gz
sha256sum /tmp/halcyon-setup.tar.gz
```

The r9 recipe carries nine patches so the live ISO gets the compositor-socket
fallback, the Mounts-step validation fix, the combo-box lifetime/window size
fixes, the Desktop step/required-package/PATH fixes, the format argv fix, and
the live kernel copy without waiting for a source release:

- `patches/0001-resolve-sde-wayland-socket.patch`
- `patches/0002-scope-mount-validation.patch`
- `patches/0003-ui-combo-lifetime-and-size.patch`
- `patches/0004-desktop-and-required-packages.patch`
- `patches/0005-format-argv.patch`
- `patches/0006-copy-live-kernel.patch`
- `patches/0007-desktop-launcher.patch`
- `patches/0008-simple-wrapper.patch`
- `patches/0009-stage3-scroll.patch`

Drop them when `_commit` is bumped past the changes.

Then build with:

```
cd ../cports
./cbuild -b bldroot-halcyon pkg halcyon-core/halcyon-setup
```

The package installs the wizard, its menu entries, icons, world map, and an
executable `/etc/skel/Desktop/Install Halcyon Linux` launcher. The live `anon`
home is created with `useradd -m`, which copies `/etc/skel`, so the launcher
appears in the live session automatically. The SDE file manager runs
executable desktop files and treats non-executable `.desktop` files as text,
so the desktop shortcut is a launcher script rather than a `.desktop` file.

To include it in the SDE ISO, add `halcyon-setup` to the `sde` package list in
`halcyon/mklive-image.sh` and build it in `halcyon/build-halcyon-iso.sh`.
