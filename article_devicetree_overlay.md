# Devicetree-Overlay für den #BananaPi erstellen

## GPIO, aber wie?

Wenn es darum geht, auf den Einplatinencomputern elektronische Komponenten anzusteuern bzw. auszulesen, wird man sowohl beim #RaspberryPi als auch beim #BananaPi auf Libraries und Dämonen im Userland gestoßen: #pigpiod, #WiringPi und ähnliche.
Schaut man sich mal oberflächlich an, wie diese gebaut sind, kriegt man es mit der Angst: Direkte Registerzugriffe per `/dev/mem` werden munter gemischt mit Kernelfunktionen verwendet, eine Prüfung ob ein Pin bereits von einem Kernelmodul bedient wird, findet nicht statt. Z. B. stellte ich erst im Zuge meiner BananaPi-Rechere fest, dass ich auf einem RaspberrPi einen Temperatursensor per gigpiod zugreife, während gleichzeitig das OneWire-Kernelmodul darauf lauscht.
Im Kernel und im klassischen Linux-Userland gibt es dagegen auch fertige Lösungen (irgendwie weniger präsent in meiner favorierten Suchmaschine): das neue GPIO-Kernel-Interface unter `/sys/bus/gpio/devices/gpiochip0` kann

* zum einen bastlerisch-explorativ mit den Tools aus dem Paket `gpiod` genutzt werden (also Achtung: gpiod ist etwas völlig anderes als pigpiod, die bauen nicht aufeinander auf, sondern beharken sich),
* zum anderen gibt es fertige Kernelmodule für bestimmte Hardware, die man an diese GPIO-Pins hängt (genau für sowas wurde das IIO-Framework im Kernel bereit gestellt).

## Das Beispiel"projekt"

Ich nehme hier mal als Beispiel einen Temperatur- und Feuchtigkeitssensor #DHT22. Dieser wird an einen PIN angeschlossen und es gibt einen IIO-basierten Kernel-Treiber dht11.ko (DHT11 und DHT22 sind offensichtlich sehr nahe verwandt und dht11.ko kann auch DHT22).
Spoiler: das Beispiel ist fies gewählt, denn der dht11.ko funktioniert am BananaPi am Ende des Tages nicht. Ich hoffe, dass einer von euch weiß, was das Problem verursacht und wie man es abgestellt bekommt :-D

## Anschluss des Sensors

Mein DHT22 kommt auf einer kleinen Platine konfektioniert mit 3 beschrifteten Anschlüssen nebst passendem 3-poligen Kabel. Der BananaPI hat im Auslieferungszustand ein 40-poliges Lochraster, in das ich eine Sockelstiftleiste eingelötet habe (vermutlich gibt es auch Sockelstiftleisten mit Klemmkontakten). Das [Anschlussschema findet man im BananaPi-Wiki](https://wiki.banana-pi.org/Banana_Pi_BPI-M2_ZERO#GPIO_PIN_define).
Der (+)-Anschluss muss mit einem 3,3V-Pin verbunden werden, Pin Nr. 1 bietet sich an. Der (-)-Anschluss mit einem Gnd-Pin, z. B. Pin Nr. 6. Der GPIO-Pin kann grundsätzlich aus einer reichlichen Auswahl gewählt werden, wir nehmen Pin Nr. 7, gemäß Schema Anschluss PA6. Also nicht verwechseln: wir stöpseln die Hardware an Pin 7, in der Software heißt das Ding aber PA6 (A=Bank 0, 6=Nr. 6 auf dieser Bank).

## Konfiguration von dht11.ko – das Devicetree Overlay

Versucht man sich diesem dht11.ko naiv zu nähern – ich hätte erwartet, dass das Kernelmodul irgendwelche Parameter bietet, über das man ihm mitteilt, an welchem Pin das Gerät sitzt – aber weit gefehlt, so tickt diese IIO-Welt nicht: sie möchte, dass wir über den #Devicetree kundgeben, welche Geräte wo an welchen Bussen hausen.
Der [eigentliche Devicetree des BananaPi](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/arch/arm/boot/dts/sun8i-h2-plus-bananapi-m2-zero.dts) findet sich bereits in den Sourcen des Linux-Kernel. Er beschreibt sozusagen, welche Geräte auf der kleinen Platine wie zusammengestöpselt sind (sogar die auf dem SOC integrierten Geräte).
Wir gedenken aber nun, ein weiteres Gerät hinzuzufügen, indem wir es an einen GPIO-Pin anschließen.
Diese Information müssen wir also in den Devicetree einbauen. Da wäre es natürlich praktisch, wir könnten genau die Aussage, "wir haben ein DHT11 an PA06 gesteckt" hinterlegen, ohne uns mit dem ganzen Devicetree auseinandersetzen zu müssen? Genau dafür gibt es Devicetree Overlays.
Damit das schick funktioniert, habe ich in der [Anleitung zur Erstellung des Boot-Images](https://write.fimidi.com/pelzvieh/systemimage-fur-einen-bananapi-aus-aktuellen-community-quellen) beim Kompilieren des Devicetree die Option `DTC_FLAGS=-@` hineingeschmuggelt. So enhält der kompilierte Devicetree die logischen Symbole und wir können im Devicetree Overlay darauf referenzieren. So sieht das dann aus (du findest die Datei auch auf [Github](https://github.com/pelzvieh/banana_resources)):
```dts
// Definitions for dht11 module
/*
Adapted from dht11.dts for Raspberrypi, by Keith Hall
Adapted by pelzi.
*/
/dts-v1/;
/plugin/;

/ {
        fragment@0 {
                target-path = "/";
                __overlay__ {
                        temperature_humidity: dht11@6 {
                                compatible = "dht22", "dht11";
                                pinctrl-names = "default";
                                pinctrl-0 = <&dht11_pins>;
                                gpios = <&pio 0 6 0>; /* PA6 (PIN 7), active high */
                                status = "okay";
                        };
                };
        };

        fragment@1 {
                target = <&pio>;
                __overlay__ {
                        dht11_pins: dht11_pins {
                                pins = "PA6";
                                function = "gpio_in";
                                bias-pull-up;
                        };
                };
        };

        __overrides__ {
                gpiopin =       <&dht11_pins>,"pins:0",
                                <&temperature_humidity>,"gpios:8";
        };
};
```
Das "Fragment 0" erzeugt einen neuen Knoten "dht11@6" und dem Symbol "temperature_humidity" direkt an der Baumwurzel. Es referenziert auf `dht11_pins` als pinctrl-0. Diese erzeugt das "Fragment 1", und zwar dort wo das Gerät auch hängt, nämlich am PIO-Controller, also unterhalb des Knotens mit dem Symbol `pio`. Es gibt an, an welchen Pins es hängt (`pins`, nur einer, mit dem Symbol `PA6`).
Schließlich definieren wir noch eine Parametrierung `gpiopin`, um einen anderen Pin angeben zu können.
Aus dieser Definition kompilieren wir ein binäres Devicetree-Overlay:
```bash
dtc dht11-banana.dts -@ -o dht11-banana.dtbo
```

## Erzeugen und Einspielen des zusammengesetzten Devicetree
Während das Bootsystem des RaspberryPi bereits eine Nachlade- und Parametrierungslogik implementiert (`/boot/config.txt`), haben wir solche Bequemlichkeit auf dem BananaPi (noch?) nicht. Man kann durchaus dem U-Boot-Loader sowohl Devicetree, als auch separate Overlays zum Laden und zusammenmischen antragen. Da dies aber ohnehin hart codiert auf dem Image ist, bevorzuge ich aktuell, den Devicetree im Vorfeld fertig zu erzeugen und aufzuspielen, da ich Fehler nicht erst im Rahmen des Bootvorganges auf einer seriellen Console zu Gesicht bekomme, sondern als gewöhnliche Meldung eines gewöhnlichen Kommandozeilentools. Auftritt fdtoverlay!
```bash
fdtoverlay -v -i sun8i-h2-plus-bananapi-m2-zero.dtb -o banana-with-dht.dtb dht11-banana.dtbo
```
Dieser Aufruf erzeugt aus dem gewöhnlichen Devicetree des BananaPi M2 Zero, `sun8i-h2-plus-bananapi-m2-zero.dtb` und dem gerade erstellten Overlay `dht11-banana.dtbo` einen vollständigen Devicetree mit DHT-Knoten namens `banana-with-dht.dtb`. Dies kopiere ich einfach auf das Image über den bisher genutzten Devicetree:
```bash
cp banana-with-dht.dtb /mnt/debinst/boot/dtbs/sun8i-h2-plus-bananapi-m2-zero.dtb
```
Diese Übungen kann man selbstmurmelnd auch auf dem laufenden Device selbst durchführen und das Ergebnis dann nach `/boot/dtbs/sun8i-h2-plus-bananapi-m2-zero.dtb` kopieren – aktiv wird es aber erst nach einem Reboot.
Ich habe leider kein Userspace-Kommando à la dtoverlay auf dem RaspberryPi finden können, um den Devicetree auf dem laufenden Gerät zu modifizieren. Das ist erstaunlich, weil es dafür eigentlich eine Kernel-API gibt, aber dtoverlay funktioniert tatsächlich nur auf einem RaspberryPi. Wer möchte ein solches Tool implementieren?

## Neustart und Nutzung
Bootet man nun den BananaPi mit diesem ergänzten Devicetree, bemerkt man sofort ein geladenes Kernelmodul
```bash
$ lsmod|grep dht
dht11                  20480  0
industrialio           65536  1 dht11
```
Und es ist fein säuberlich hinterlegt, dass und wofür wir unseren Pin PA6 verwenden:
```
$ sudo gpioinfo
gpiochip0 - 224 lines:
[...]
	line   6:      unnamed    "dht11@6"   input  active-high [used]
[...]
```
Der DHT-Treiber stellt uns fertige Geräte zum Auslesen zur Verfügung:
```bash
$ ls -l /sys/bus/iio/devices/iio\:device0/
insgesamt 0
-r--r--r-- 1 root root 4096 15. Jan 13:16 dev
-rw-r--r-- 1 root root 4096 15. Jan 13:16 in_humidityrelative_input
-rw-r--r-- 1 root root 4096 15. Jan 13:16 in_temp_input
-r--r--r-- 1 root root 4096 15. Jan 13:16 name
lrwxrwxrwx 1 root root    0 15. Jan 13:16 of_node -> ../../../../firmware/devicetree/base/dht11@6
drwxr-xr-x 2 root root    0 15. Jan 11:30 power
lrwxrwxrwx 1 root root    0 15. Jan 13:16 subsystem -> ../../../../bus/iio
-rw-r--r-- 1 root root 4096 14. Jan 23:08 uevent
```
Wir können die aktuellen Messwerte einfach durch Lesen an den device files `in_humidityrelative_input` und `in_temp_input` ermitteln.

## Das Problem mit dht11

Wie oben schon angekündigt, gibt es hier aber leider ein verdrießliches Problem – das Auslesen funktioniert nicht:
```bash
$ cat /sys/bus/iio/devices/iio\:device0/in_humidityrelative_input 
cat: '/sys/bus/iio/devices/iio:device0/in_humidityrelative_input': Die Wartezeit für die Verbindung ist abgelaufen
```
Parallel dazu nennt uns `dmesg`:
```
[52801.685052] dht11 dht11@6: Only 18 signal edges detected
```
Wie ich nach etwas Analyse definitiv bestätigen kann: der Treiber weckt den DHT11-Sensor auf und dieser fängt an, seine Messwerte zu schicken.
Das Dumme ist, dass der BananaPi nur ein Bruchteil der tatsächlich entstandenen Flanken am Signalpin via Interrupt einfängt. Wir wissen von den eingefangenen Flanken zwar hinreichend genau die Zeitstempel, aber es fehlen halt ungefähr 70 Flanken!
Warum es so lange Totzeiten zwischen den Interrupts gibt, ist mir noch nicht klar. Für Hinweise wäre ich extrem dankbar!

## Was funktioniert denn nun?
Wenn du z. B. nur ein paar Ein-Aus-Sensoren (auch Bewegungsmelder...) und LEDs verbinden möchtest, funktioniert das auf analoge Weise durchaus. Du kannst analog des `fragment@1` oben Pins für Input oder Output konfigurieren, inkl. Pull-Up/Down-Widerständen (Input) oder Drive-Konfiguration (Output).
Und es gibt eine riesige Menge an [IIO-Device-Treibern](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/drivers/iio) fertig in den Linux-Quellen, die eigentlich nur darauf warten in eigenen Projekten genutzt zu werden.
Wie du den Treiber eines GPIO-basierten Gerätes am BananaPi in Betrieb bekommst, sollte dieser Artikel dir jetzt verraten haben. Ich wäre dir dankbar, wenn du erfolgreiche Anbindungen von anderen Sensoren auch veröffnetlichen würdest!

## Vertiefende Dokumentation

1. [Device Tree Usage](https://elinux.org/Device_Tree_Usage)
1. [Device Tree Overlay Notes](https://www.kernel.org/doc/html/latest/devicetree/overlay-notes.html)
1. [Bindings der IIO-Treiber](https://www.kernel.org/doc/Documentation/devicetree/bindings/iio/)
