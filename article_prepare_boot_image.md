# Systemimage für einen #BananaPi aus aktuellen Community-Quellen

## Warum die Übung?
Die etablierten Informationsquellen zum BananaPi (z. B. das [Wiki](https://wiki.banana-pi.org/Banana_Pi_BPI-M2_ZERO#Source_code)) verlinken einmalig fertig gebaute Images und bieten Anleitung, den Rechner auf dieser Basis in Betrieb zu nehmen.

Warum ist das ein Problem?

1. Vulnerabilities: die Images wurden von irgendeinem vergangenen, Jahre alten, Releasestand von Linux-Kernel und Distributionen gebaut. Inzwischen dürften hunderte von Schwachstellen dieses Standes bekannt geworden sein, deren Korrekturen aber nicht in den Images enthalten sind.
1. Unklare Herkunft: es ist nicht transparent, wer diese Images gebaut hat, welche Review-Prozesse stattgefunden haben und welche Änderungen an den Upstream-Quellen durchgeführt wurden und zu welchem Zweck.
1. Mangelnde Professionalität: unter den veröffentlichten Images befinden sich solche, die nicht durch einen Buildprozess erzeugt wurden, sondern einen Snapshot eines manuell aufgesetzten und genutzten Systems darstellen. Es finden sich insbesondere lange Shell-Histories, konfigurierte Zugänge zu WLANs taiwanesicher(?) Shared Office-Standorte.
1. Fehlende Versionskontrolle: es werden auf unterschiedlichen Seiten unterschiedliche Stände auf unterschiedlichen Filesharing-Plattformen referenziert, von denen einige nicht mehr richtig funktionieren.
All dies zusammen genommen macht diese Quellen zu einem El Dorado für persistente Angriffe ([APTs](https://de.wikipedia.org/wiki/Advanced_Persistent_Threat)). 
Sowas kommt mir nicht ins Haus und euch hoffentlich nicht ins Unternehmen. Da sind wir uns doch einig..?!?
Dann kann's ja losgehen!

## Ziele
1. Ein Image zum Aufspielen auf Micro-SD-Karte wird erstellt
1. Ein #BananaPi M2 Zero kann von einer mit diesem Image bespielten SD-Karte booten
1. Der Bootvorgang verläuft unfallfrei in ein nutzbares System
1. Funktionsfähig sind zumindest: Mini-HDMI (Bildschirmausgabe), Micro-USB-Port, GPIO (PINs), WLAN
1. Das Gerät bucht sich automatisch in das konfigurierte WLAN ein und ist remote über ssh erreichbar (denn das Gerät hat keine fertig konfektionierte Ethernet-Schnittstelle und ich bin zu faul um die dafür nötigen PINs einzulöten und an per Kabeln an eine Ethernet-Buchse zu fummeln).

## Anlegen des Images
Wir legen das spätere Image als lokale Datei auf einem Debian-Linux-Rechner (gerne virtuell...) an, machen diese via Loop-Device als Blockgerät verfübar und richten dieses richtig ein. Dieses Vorgehen hilft gegen die Versuchung, die SD-Karte zur Unzeit schonmal ins Zielgerät einzulegen und darauf manuell weiter zu fummeln, bis etwas funktioniert.
Das Vorgehen hier folgt eigentlich nur Schritt für Schritt der [entsprechenden Anleitung](https://www.debian.org/releases/bullseye/armhf/apds03.de.html) von Debian! Wenn dieser Artikel schon einige Jahre alt ist wenn du das liest, solltest du dort nach einer aktuellen Anleitung suchen.
(todo: Hier fehlt eine Aufstellung der auf dem Linux-Rechner benötigten Pakete und sonstigen Voraussetzungen; beispielsweise fehlt die Warnung, dass das alles nicht auf arm64-Umgebungen funktioniert, weil ...)
```bash
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
An dieser Stelle haben wir die benötigten Softwarepakete schon in unserem Image liegen, wir können Programme auf der Zielplattform ausführen und sind gerade in die Systemumgebung unseres aufzusetzenden Rechners geschlüpft. Weiter geht's, nun _im_ chroot:
```bash
/debootstrap/debootstrap --second-stage
editor /etc/adjtime
```
Das ist jetzt ein bisschen doof, weil du nicht siehst, was man da reinschreiben kann. Aber du kannst die Man-Page bzw. die Debian-Anleitung zu Rate ziehen :-)
```bash
dpkg-reconfigure tzdata
editor /etc/systemd/network/eth0.network
editor /etc/systemd/network/wlan0.network
editor etc/wpa_supplicant/wpa_supplicant.conf
```
Dokumentation nebst Beispielen findest du in der man-Page [wpa-supplicant.conf](https://manpages.debian.org/bullseye/wpasupplicant/wpa_supplicant.conf.5.en.html)
```bash
mkdir root/.ssh
editor root/.ssh/authorized_keys 
```
Hier kopierst du den public ssh-key rein, mit dem du dich am laufenden System anmelden willst. Achtung, es ist KEIN Passwort-Login für root möglich! Vielleicht möchtest du auch direkt an dieser Stelle einen User anlegen, für diesen einen ssh-Key hinterlegen, ihn für sudo berechtigen usw. Das halte ich für eine gute Idee, ist aber normales Linux-Alltagsgeschäft, da brauchst du ja keine Tipps von mir.
```bash
editor etc/resolv.conf
apt install openssh-server
editor etc/apt/sources.list.d/security.list # hier trägst du die apt source für Security-Updates deiner Distribution ein
editor etc/apt/sources.list.d/firmware.list # hier trägst du die apt source ein, aber die Sektion non-free
apt update
apt install locales && dpkg-reconfigure locales
apt install console-setup && dpkg-reconfigure keyboard-configuration
apt install linux-image-armmp-lpae
apt install firmware-linux bluez-firmware firmware-atheros firmware-bnx2 firmware-bnx2x firmware-brcm80211 firmware-cavium firmware-ipw2x00 firmware-iwlwifi firmware-libertas firmware-qcom-soc firmware-qlogic firmware-ti-connectivity firmware-zd1211 
```
Viel hilft viel! Naja, ehrlich gesagt müsste sich mal jemand die Mühe machen herauszusuchen, welche Firmware-Pakete der BananaPi wirklich braucht...
```bash
systemctl add-requires wpa_supplicant.service systemd-networkd-wait-online.service
systemctl add-wants network-online.target wpa_supplicant.service
editor /lib/systemd/system/wpa_supplicant.service
```
Im ausgelieferten Zustand würde wpa_supplicant nur über dbus lauschen, daher musst du `-iwlan0 -c/etc/wpa_supplicant/wpa_supplicant.conf` zu ExecStart hinzufügen. Kennst du eine saubere Lösung für das Problem?
```bash
echo "banana" > /etc/hostname
editor /etc/dhcpcd.conf # enable option "hostname"
exit
```
Nun sind wir wieder draußen aus dem Image. Die Userland ist jetzt fertig. Was fehlt, sind Bootloader und die Systemkonfiguration in /boot.
Das U-Boot (den Bootloader) bauen wir wie folgt:
```bash
git clone git://git.denx.de/u-boot.git
cd u-boot
make bananapi_m2_zero_defconfig
make CROSS_COMPILE=arm-linux-gnueabihf-
cd ..
```
Eine Device-Tree-Definition für den BananaPi M2 Zero gibt es glücklicherweise fertig im Linux-Sourcetree. Wir können sie also ebenfalls bauen:
```bash
apt source linux-image-5.10.0-20-armmp-lpae
cd linux-5.10.*/
ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- make defconfig
ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- make DTC_FLAGS=-@ dtbs
cd ..
```
Eine Bootloader-Konfiguration leihen wir uns von TuryRx in github aus. Das U-Boot-Script wird allerdings fest codierte Namen für Kernel und InitRAMFS verwenden, während diese Dateien im Debian-Paket mit Versionsnummern verziert sind. Deshalb kopieren wir die gerade installierten einfach.
```bash
wget https://github.com/TuryRx/Banana-pi-m2-zero-Arch-Linux/raw/master/boot.cmd
mkdir /mnt/debinst/boot/dtbs
sudo cp u-boot/u-boot-sun8i-h2-plus-bananapi-m2-zero.dtb /mnt/debinst/boot/dtbs/sun8i-h2-plus-bananapi-m2-zero.dtb
sudo cp /mnt/debinst/boot/vmlinuz-5.10.0-20-armmp-lpae /mnt/debinst/boot/zImage
sudo cp /mnt/debinst/boot/initrd.img-5.10.0-20-armmp-lpae /mnt/debinst/boot/initramfs-linux.img
sudo mkimage -A arm -O linux -T script -C none -a 0 -e 0 -n "BananaPI boot script" -d boot.cmd /mnt/debinst/boot/boot.scr
sudo umount /mnt/debinst 
sudo dd if=u-boot-sunxi-with-spl.bin of=/dev/loop0 bs=1024 seek=8 # ja, so habe ich auch gekuckt: so lädt die Firmware des BananaPi ihren Bootloader...
sudo losetup -d /dev/loop0
```
Fertig ist das Image! Du kannst es jetzt auf eine Micro-SD-Karte übertragen und einen BananaPi M2 Zero damit starten.
