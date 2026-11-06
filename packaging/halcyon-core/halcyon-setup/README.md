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

The r2 recipe carries two patches so the live ISO gets the compositor-socket
fallback and the Mounts-step validation fix without waiting for a source
release:

- `patches/0001-resolve-sde-wayland-socket.patch`
- `patches/0002-scope-mount-validation.patch`

Drop them when `_commit` is bumped past both changes.

Then build with:

```
cd ../cports
./cbuild -b bldroot-halcyon pkg halcyon-core/halcyon-setup
```

The package installs the wizard, its desktop entries, icons, world map, and
`/etc/skel/Desktop/HalcyonSetup.desktop`. The live `anon` home is created with
`useradd -m`, which copies `/etc/skel`, so the desktop link appears in the live
session automatically.

To include it in the SDE ISO, add `halcyon-setup` to the `sde` package list in
`halcyon/mklive-image.sh` and build it in `halcyon/build-halcyon-iso.sh`.
