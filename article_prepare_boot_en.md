# System image for a #BananaPi from current community sources.
15. Januar 2023
## Why bother?

The established sources of information about the BananaPi (e.g. the [Wiki](https://wiki.banana-pi.org/Banana_Pi_BPI-M2_ZERO#Source_code)) link to ready-built images once and offer instructions for getting the computer up and running on this basis.

Why is this a problem?

1. vulnerabilities: the images were built from some past, years old, release of Linux kernel and distributions. In the meantime, hundreds of vulnerabilities of this state have probably become known, but their fixes are not included in the images.
2. unclear origin: it is not transparent who built these images, what review processes took place and what changes were made to the upstream sources and for what purpose.
3. lack of professionalism: among the published images are those that were not created by a build process, but represent a snapshot of a manually set up and used system. In particular, there are long shell histories, configured accesses to WLANs of Taiwan-safe(?) shared office locations.
4. lack of version control: different sites reference different versions on different file sharing platforms, some of which no longer work properly. All this combined makes these sources an El Dorado for persistent attacks (APTs).

I'm not going to run anything like that in my house, and I hope you're not going to do so in your company. We agree on that, don't we?!?
Then let's get started!

## Goals

* an image for uploading to a micro SD card is created
* a #BananaPi M2 Zero can boot from a SD-card with this image on it
* the boot process runs accident-free into a usable system
* functional are at least: Mini-HDMI (screen output), Micro-USB port, GPIO (PINs), WLAN
* the device automatically logs into the configured WLAN and is remotely accessible via ssh (because the device doesn't have a ready-made Ethernet interface and I'm too lazy to solder the necessary PINs and to fiddle with cables to an Ethernet socket).

## Creating the image

We create the image as a local file on a Debian Linux machine (virtual if you like...), make it available as a block device via loop device and set it up correctly. This procedure helps to avoid the temptation to insert the SD card into the target device at the wrong time and to fiddle with it manually until something works.
The procedure here actually only follows the [corresponding instructions](https://www.debian.org/releases/bullseye/armhf/apds03.de.html) of Debian step by step! If this article is already some years old when you read this, you should look there for a current manual.
(todo: Here is missing a list of the packages and other requirements needed on the Linux machine; for example the warning is missing that all this does not work on arm64 environments because ...)
```
dd if=/dev/zero of=bananapi_debian.img bs=1G count=3
sudo losetup -f bananapi_debian.img 
sudo fdisk /dev/loop0 
sudo mkfs.ext4 -O ^metadata_csum,^64bit /dev/loop0p1
sudo losetup -d /dev/loop0 
sudo losetup -P -f bananapi_debian.img 
sudo mkfs.ext4 -O ^metadata_csum,^64bit /dev/loop0p1 
sudo mkdir /mnt/debinst
sudo mount /dev/loop0p1 /mnt/debinst/
sudo debootstrap --arch=armhf --foreign --include=binfmt-support,wpasupplicant,dhcpcd5 bullseye /mnt/debinst
sudo cp /usr/bin/qemu-arm-static /mnt/debinst/usr/bin/
sudo LANG=C.UTF-8 chroot /mnt/debinst qemu-arm-static /bin/bash
```

At this point we already have the required software packages in our image, we can run programs on the target platform and we have just slipped into the system environment of our machine to be set up. Moving on, now in the chroot:

```
/debootstrap/debootstrap --second-stage
editor /etc/adjtime
```

Now this is a pity, because you can't see what to put in there. But you can consult the man page or the Debian manual :–)

```
dpkg-reconfigure tzdata
editor /etc/systemd/network/eth0.network
editor /etc/systemd/network/wlan0.network
editor etc/wpa_supplicant/wpa_supplicant.conf
```


Documentation and examples can be found in the [man page wpa-supplicant.conf](https://manpages.debian.org/bullseye/wpasupplicant/wpa_supplicant.conf.5.en.html)

```
mkdir root/.ssh
editor root/.ssh/authorized_keys 
```

Here you copy the public ssh-key you want to use to log in to the running system. Attention, NO password login is possible for root! You may also want to create a user here, set an ssh-key for her, authorize her for sudo etc.. I think that's a good idea, but it's normal everyday Linux business, so you don't need any tips from me.

```
editor etc/resolv.conf
apt install openssh-server
editor etc/apt/sources.list.d/security.list # here you enter the apt source for security updates of your distribution
editor etc/apt/sources.list.d/firmware.list # here you enter the apt source, but the section non-free
apt update
apt install locales && dpkg-reconfigure locales
apt install console-setup && dpkg-reconfigure keyboard-configuration
apt install linux-image-armmp-lpae
apt install firmware-linux bluez-firmware firmware-atheros firmware-bnx2 firmware-bnx2x firmware-brcm80211 firmware-cavium firmware-ipw2x00 firmware-iwlwifi firmware-libertas firmware-qcom-soc firmware-qlogic firmware-ti-connectivity firmware-zd1211 
```

Well, to be honest, someone should take the trouble to find out which firmware packages the BananaPi really needs...

```
systemctl add-requires wpa_supplicant.service systemd-networkd-wait-online.service
systemctl add-wants network-online.target wpa_supplicant.service
editor /lib/systemd/system/wpa_supplicant.service
```

As shipped, wpa_supplicant would only listen via dbus, so you need to add `-iwlan0 -c/etc/wpa_supplicant/wpa_supplicant.conf` to ExecStart. Do you know of a clean solution to this problem?

```
echo "banana" > /etc/hostname
editor /etc/dhcpcd.conf # enable option "hostname
exit
```

Now we are out of the image again. The userland is now ready. What is missing are bootloader and the system configuration in /boot.
The U-Boot (the bootloader) we build as follows:

```
git clone git://git.denx.de/u-boot.git
cd u-boot
make bananapi_m2_zero_defconfig
make CROSS_COMPILE=arm-linux-gnueabihf-
cd ..
```


A device tree definition for the BananaPi M2 Zero fortunately exists ready-made in the Linux sourcetree. So we can build it as well:

```
apt source linux-image-5.10.0-20-armmp-lpae
cd linux-5.10.*/
make CROSS_COMPILE=arm-linux-gnueabihf- defconfig
make CROSS_COMPILE=arm-linux-gnueabihf- DTC_FLAGS=-@ dtbs
cd ..
```


We borrow a bootloader configuration from TuryRx in github. However, the U-Boot script will use hardcoded names for kernel and InitRAMFS, while these files are decorated with version numbers in the Debian package. Therefore, we simply copy the ones we just installed.

```
wget https://github.com/TuryRx/Banana-pi-m2-zero-Arch-Linux/raw/master/boot.cmd
mkdir /mnt/debinst/boot/dtbs
sudo cp u-boot/u-boot-sun8i-h2-plus-bananapi-m2-zero.dtb /mnt/debinst/boot/dtbs/sun8i-h2-plus-bananapi-m2-zero.dtb
sudo cp /mnt/debinst/boot/vmlinuz-5.10.0-20-armmp-lpae /mnt/debinst/boot/zImage
sudo cp /mnt/debinst/boot/initrd.img-5.10.0-20-armmp-lpae /mnt/debinst/boot/initramfs-linux.img
sudo mkimage -A arm -O linux -T script -C none -a 0 -e 0 -n "BananaPI boot script" -d boot.cmd /mnt/debinst/boot/boot.scr
sudo umount /mnt/debinst 
sudo dd if=u-boot-sunxi-with-spl.bin of=/dev/loop0 bs=1024 seek=8 # yes, that's how I looked too: this is where the BananaPi's firmware loads its bootloader from...
sudo losetup -d /dev/loop0
```

The image is ready! You can now transfer it to a micro SD card and boot a BananaPi M2 Zero with it.

Translation based on www.DeepL.com/Translator (free version)
