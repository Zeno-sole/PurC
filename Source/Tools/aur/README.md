# PurC AUR package packaging script

## PurC release version Packaging script

```bash
# The following operations are done under the Arch Linux release version.
# Other Linux distributions can be referenced.
$ cd aur/purc

# Modify the version number to the current latest release number
$ vim PKGBUILD
# eg., pkgver=0.8.1
# Save and exit

# Update the check value of the PurC release version and generate a .SRCINFO file
$ updpkgsums && makepkg --printsrcinfo > .SRCINFO

# Compile and package the PurC release version
$ makepkg -sf

# Install the PurC package
$ yay -U purc-*.tar.zst

# Compile, package and install
$ makepkg -sfi

# Online installation via AUR
$ yay -S purc
```

## PurC development version Packaging script

```bash
# The following operations are done under the Arch Linux release version.
# Other Linux distributions can be referenced.
$ cd aur/purc-git

# Update the check value of the PurC development version and generate a .SRCINFO file
$ updpkgsums && makepkg --printsrcinfo > .SRCINFO

# Compile and package the PurC development version
$ makepkg -sf

# Install the PurC package
$ yay -U purc-git-*.tar.zst

# Compile, package and install
$ makepkg -sfi

# Online installation via AUR
$ yay -S purc-git
```