# Bizarre Interrupt-Probleme des #BananaPi gelöst!

Wie schon geschrieben, mein #BananaPi M2 Zero konnte ja nicht mit dem DHT11-Kernelmodul den angeschlossenen Sensor auslesen. Von rund 80 erwarteten Flanken lösten gerade immer knapp 20 einen IRQ aus. Ich habe auch schon Workarounds durch Polling im Userspace und eine polling-Version des DHT11-Kernelmoduls vorgestellt. Die gute Nachricht: der Mist kann weg!

## Diskussionen mit den Kernel-Entwicklern

Fruchtbare Anstöße gab es, als ich versucht habe, den Polling-Driver in den Kernel zu bekommen. Das dürfe eigentlich gar nicht nötig sein, nicht bei diesem Board, hieß es. Es müsse ein Problem mit meinem Setup geben.
Nun, zum Glück ist das Setup ja aus nachvollziehbaren Quellen mit reproduzierbaren Schritten entstanden, da weiß man ja, wo's her kommt.

##  Fakten, Fakten, Fakten

Ich habe also analysiert, was eigentlich beim Treiber so ankommt. Allerdings sind wir jetzt am Puls der Kernel-Zeit, d. h. ich musste erstmal den aktuellen staging-testing-Branch des Kernel für die Banane übersetzen (Image, Module, devicetree) und ein initramfs-Image generieren. Letzteres verursacht wieder Puls, weil der ARM64-Laptop nicht in der Lage ist, armhf-Binaries auszuführen. Aber dank update-ramfs auf dem BananaPi selbst, konnte auch dieses Problem wieder gelöst werden.

Meine neue Erkenntnis des Tages: “dynamic debug” ist eine Kernel-Funktion, über die man ganz bestimmte debug-Ausgaben im laufenden Kernel an- und ausknipsen kann:

    echo "file dht11.c +p" | sudo tee /proc/dynamic_debug/control

..schaltet das Debugging unseres dht11-Kernelmoduls an.

Ergebnis der Übung eigentlich recht diffus: so alle 150-300µs trudelt mal ein IRQ ein, über die Hälfte geht verschütt.
CPU-Leistung, Interrupt-Geschehen, Speicher, Kernel-Meldungen: alles unauffällig. Was ist hier los?

## Des Bananenproblems Kern

Ich weiß gar nicht so genau warum, aber ich hatte immer das Gefühl, dass das Problem mit der Verarbeitung von GPIO-Signalen zu IRQs des SoC zu tun haben muss – und nicht mit etwas, was Betriebssystem und CPUs so treiben. Diesem Gefühl folgend arbeitete ich mich mäßig inspiriert durch ein Datasheet des Allwinner H3 (zu finden in den [Untiefen des Internet](https://www.mikrocontroller-elektronik.de/dht22-am2302-luftfeuchte-und-temperatursensor/), warum auch immer dort und nicht beim Hersteller...). Architektur des SoC, Busse, Bridges. Beschreibung der GPIOs, Register der PA-Bank, hmhm.

Oha! Hinter den Registern der PG-Bank kommt nochmal die PA-Bank dran: Kapitel 4.2.55ff beschäftigen sich mit Registern zur Kontrolle der Interrupts aus der PA-Bank.

Und dann fällt mir ins Auge “4.22.2.61. PA External Interrupt Debounce Register”. Debounce, also ein Filter gegen Interrupt-Feuer durch prellende (mechanische) Taster, sowas kann das Gerät? Und der Default ist Abriegelung mit 32kHz, scheinen die kargen Infos nahe zu legen. Das könnte die Erklärung sein und war dann auch!

Geschwind in Devicetree-Doku und Kernel-Sourcen geblättert, wie dieses Register bespielt wird und flugs den Devicetree-Overlay ergänzt um eine Einstellung des &pio:

   input-debounce = <5 0>;

...und schon funktioniert der Treiber!

Und nun?

Ihr findet [das aktualisierte Devicetree Overlay](https://github.com/pelzvieh/banana_resources/blob/main/dht11.dts) in meinem Repository von Bananen-Ressourcen.

Daneben auch einen veränderten DHT11-Devicetreiber, der nur noch auf falling edges lauscht, da die low-Pegel des Sensors keine Information tragen. Der ist kein Muss, aber meinen Messungen nach funktioniert er noch einen Tick zuverlässiger als der Original-Treiber und letzter benötigt einen eher noch niedrigeren debounce-Eintrag: mit input-debounce = <1 0> tut er's dann auch ganz robust.

Solltet ihr die Polling-Version des Treiber gebaut und per device tree eingebunden haben: das kann jetzt glücklicherweise entfallen.

Was mir noch unklar ist: ob das Fehlen einer input-debounce-Konfiguration nicht nachgerade ein Bug im Devicetree (aus den Kernel-Sourcen) ist. Denn an der A-Bank hängen auch andere Geräte, nicht nur über die Steckerleiste frei nutzbare GPIOs. Dass z. B. die seriellen Schnittstellen mit dieser Schaumbremse glücklich sein sollen, kann ich mir nur schwer vorstellen.

Und seit der Umstellung ist die Kerneltask sugov:0, die mich vorher in der Anzeige von top wegen ihres (angeblichen) CPU-Konsums verwirrt hat, von dort verschwunden. Die Änderung ist also alles andere als frei von Nebenwirkungen, schauen wir mal, wie sie sich bewährt.

