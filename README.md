# NOVA OS 0.1 — ISO build project

NOVA OS is a real Debian 13 + KDE Plasma 6 live Linux distribution with a top
menu bar, centered dock, bundled Linux games, graphical installer and no NOVA ads.

## Build on GitHub

Push to branch nova-os-build to trigger .github/workflows/build-iso.yml.
Open the Actions tab of this repository and inspect Build NOVA OS ISO.
After a successful run, download the nova-os-amd64-iso artifact and unzip it
to obtain nova-os-amd64.iso and its checksum. The build is NOT yet tested.
GitHub runner disk and artifact quotas may prevent large image builds.

## Local build

On Debian 13, with root access and 25 GB or more of free space:
sudo apt update
sudo apt install live-build debootstrap xorriso squashfs-tools dosfstools mtools isolinux syslinux-common grub-pc-bin grub-efi-amd64-bin librsvg2-bin
sudo ./build.sh

Test the result in a virtual machine before hardware installation.
Installation can destroy existing files if the wrong disk is chosen.
This system is Linux, not Apple macOS. Proton can run some Windows games if
installed separately, but not all games will be compatible.
