# OrcaSlicer_BambuLocal

**OrcaSlicer für Bambu-Lab-Drucker ohne Cloud.** Dieser Fork von [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer) bringt Funktionen, die es sonst nur bei Verbindung über die Bambu-Cloud gibt, in den LAN-Only-Modus. Der Drucker bleibt komplett im eigenen Netzwerk, und man muss trotzdem auf (fast) nichts verzichten.

## Worum geht es?

Bambu-Lab-Drucker lassen sich auf zwei Arten mit dem PC verbinden:

- **Cloud-Modus:** Drucker und Slicer laufen über die Server von Bambu Lab. Das braucht ein Bambu-Konto und eine Internetverbindung, dafür funktioniert alles.
- **LAN-Only-Modus:** Der Drucker spricht nur mit Geräten im eigenen Netzwerk, ohne Konto und ohne Internet. Viele wählen das aus Datenschutzgründen oder weil sie ihre Geräte nicht von einem fremden Server abhängig machen wollen.

Im LAN-Only-Modus fehlen bei Druckern wie dem P1S oder A1 aber wichtige Funktionen: Der Speicher (SD-Karte) lässt sich in OrcaSlicer nicht öffnen, Objekte können während des Drucks nicht übersprungen werden, und nach jedem Neustart muss man den Drucker neu auswählen. Diese Funktionen hängen nicht an der Hardware, sondern am Weg über die Cloud.

Dieser Fork holt sie in den LAN-Only-Modus zurück. Er spricht dafür direkt mit dem Drucker, über dieselben Schnittstellen, die der Drucker auch im LAN-Modus anbietet (FTPS für die SD-Karte, MQTT für Befehle).

## Was funktioniert im LAN-Only-Modus?

| Funktion | OrcaSlicer (Original) | Dieser Fork |
|---|:---:|:---:|
| Drucken über das Netzwerk, Kamera, Status | ✅ | ✅ |
| Speicher-Tab: Dateien auf der SD-Karte mit Vorschaubild, Druckzeit und Gewicht ansehen | ❌ | ✅ |
| Dateien von der SD-Karte herunterladen und löschen | ❌ | ✅ |
| Dateien vom PC auf die SD-Karte hochladen | ❌ | ✅ |
| Druck direkt von der SD-Karte starten | ❌ | ✅ |
| Richtiges Datum der Druckaufträge (der Drucker hat ohne Cloud keine verlässliche Uhr) | ❌ | ✅ |
| Gerade gestarteter Druck steht im Speicher-Tab sofort oben, mit Vorschaubild | ❌ | ✅ |
| Verbindung bleibt stabil, auch wenn der Drucker kurz nichts meldet | ❌ | ✅ |
| Objekte während des Drucks überspringen | ❌ | ✅ |
| Beim Start automatisch mit dem letzten Drucker verbinden | ❌ | ✅ |
| Bambu-Handy-App, Zugriff von unterwegs | ❌ | ❌ |

Nicht möglich ist nur, was zwingend über die Server von Bambu Lab läuft: die Handy-App (Bambu Handy) und damit der Zugriff von außerhalb des eigenen Netzwerks.

## Für wen ist das?

- Für Besitzer eines Bambu-Lab-Druckers der Serien **P1** (P1P, P1S) oder **A1** (A1, A1 mini), die den Drucker im LAN-Only-Modus betreiben. Getestet ist alles an einem P1S.
- Für Windows (x64 und ARM64).

Drucker, die OrcaSlicer schon von sich aus voll im LAN unterstützt (z. B. X1), und Drucker im Cloud-Modus verhalten sich wie im Original-OrcaSlicer. Abgesehen von den oben genannten Funktionen ist der Fork ein ganz normales OrcaSlicer und wird regelmäßig mit dem Original abgeglichen.

## Download

Der letzte gültige Build steht unter [Releases](https://github.com/83nn1/OrcaSlicer_BambuLocal/releases/latest), aktuell **V0.5.3**. Welche Version installiert ist, zeigen das Startfenster und *Hilfe → Über*. Direkt herunterladen:

| Windows | Installer | Portable |
|---|---|---|
| x64 | [OrcaSlicer_BambuLocal_Windows_x64_Installer.exe](https://github.com/83nn1/OrcaSlicer_BambuLocal/releases/latest/download/OrcaSlicer_BambuLocal_Windows_x64_Installer.exe) | [OrcaSlicer_BambuLocal_Windows_x64_portable.zip](https://github.com/83nn1/OrcaSlicer_BambuLocal/releases/latest/download/OrcaSlicer_BambuLocal_Windows_x64_portable.zip) |
| ARM64 | [OrcaSlicer_BambuLocal_Windows_arm64_Installer.exe](https://github.com/83nn1/OrcaSlicer_BambuLocal/releases/latest/download/OrcaSlicer_BambuLocal_Windows_arm64_Installer.exe) | [OrcaSlicer_BambuLocal_Windows_arm64_portable.zip](https://github.com/83nn1/OrcaSlicer_BambuLocal/releases/latest/download/OrcaSlicer_BambuLocal_Windows_arm64_portable.zip) |

## So geht's los

1. Am Drucker den **LAN-Only-Modus** einschalten und den **Access Code** notieren (am Druckerdisplay unter den Netzwerkeinstellungen).
2. OrcaSlicer_BambuLocal installieren oder die Portable-Version entpacken und starten.
3. Im Tab *Gerät* den Drucker hinzufügen und den Access Code eingeben.

Ab dann stehen Speicher-Tab, Objekte überspringen und die übrigen Funktionen zur Verfügung. Beim nächsten Start verbindet sich Orca von selbst wieder mit dem Drucker.

## Mehr erfahren

Wie die einzelnen Funktionen arbeiten, was getestet ist und wie man den Fork selbst baut, steht im **[Wiki](https://github.com/83nn1/OrcaSlicer_BambuLocal/wiki)**:

- [Speicher-Tab (SD-Karte)](https://github.com/83nn1/OrcaSlicer_BambuLocal/wiki/Speicher-Tab)
- [Objekte überspringen](https://github.com/83nn1/OrcaSlicer_BambuLocal/wiki/Objekte-überspringen)
- [Automatisch verbinden](https://github.com/83nn1/OrcaSlicer_BambuLocal/wiki/Automatisch-verbinden)
- [Teststand](https://github.com/83nn1/OrcaSlicer_BambuLocal/wiki/Teststand)
- [Bauen, Versionen und Releases](https://github.com/83nn1/OrcaSlicer_BambuLocal/wiki/Bauen-und-Versionen)

---

Dies ist ein privates Projekt und steht in keiner Verbindung zu Bambu Lab. Original-Projekt: [OrcaSlicer/OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer)
