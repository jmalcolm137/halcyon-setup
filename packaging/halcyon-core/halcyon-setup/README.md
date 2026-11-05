# halcyon-core/halcyon-setup

Copy this directory to the Halcyon cports checkout:

```
cp -a packaging/halcyon-core/halcyon-setup ../cports/halcyon-core/
```

Before building, replace `_commit` with the released commit and update
`sha256` with the digest of the GitHub archive:

```
curl -L -o /tmp/halcyon-setup.tar.gz \
    https://github.com/jmalcolm137/halcyon-setup/archive/<commit>.tar.gz
sha256sum /tmp/halcyon-setup.tar.gz
```

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
