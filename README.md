# OrcaSlicer_BambuLocal

Fork von [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer) für Bambu-Lab-Drucker im LAN-Only-Modus (P1/A1):

- **Zugriff auf den Speicher (SD-Karte)** unter *Gerät → Speicher*, inkl. Hochladen (siehe [unten](#hochladen))
- **Objekte überspringen** während des Drucks (siehe [unten](#objekte-überspringen))

## Problem

Im LAN-Only-Modus zeigt OrcaSlicer unter *Gerät → Speicher* bei Druckern wie dem P1S nur:

> Das Durchsuchen von Dateien im Speicher wird im LAN-Only-Modus nicht unterstützt.

Der Speicher-Tab holt die Dateien normalerweise über einen Tunnel des Bambu-Netzwerk-Plugins. Den bieten P1- und A1-Drucker im LAN-Modus nicht an. Die SD-Karte ist aber per FTPS erreichbar (Port 990, Benutzer `bblp`, Passwort = LAN-Access-Code). Diesen Weg nutzt z. B. auch die Home-Assistant-Integration [ha-bambulab](https://github.com/greghesp/ha-bambulab).

## Lösung

`src/slic3r/GUI/Printer/LanFtpsTunnel.cpp` ersetzt diesen Tunnel durch einen eigenen, der dieselben Anfragen versteht und per FTPS (libcurl) direkt an die SD-Karte weitergibt:

| Anfrage | Umsetzung |
|---|---|
| Dateiliste | `LIST /`, `/cache`, `/model` (Modelle, `.3mf`) bzw. `/timelapse` (Zeitraffer) |
| Vorschau, Druckzeit, Gewicht | `.3mf` wird einmal geladen, im Temp-Ordner zwischengespeichert (max. 512 MB) und daraus gelesen |
| Zeitraffer-Vorschaubilder | `/timelapse/thumbnail/<name>.jpg` |
| Herunterladen | `RETR`, in Blöcken mit MD5-Prüfung, abbrechbar |
| Löschen | `DELE` (bei Zeitraffern samt Vorschaubild) |
| Hochladen | `STOR /<name>`, gestreamt, mit Größen- und MD5-Prüfung, abbrechbar |
| Datum der Druckaufträge | Zeitstempel im Kopf der `.3mf` (siehe unten) |

### Datum der Druckaufträge

Im LAN-Only-Modus hat der Drucker keine verlässliche Uhr: Nach jedem Aus- und Einschalten läuft sie von einem alten Datum weiter. Die Dateizeiten auf der SD-Karte sind dadurch unbrauchbar, bei einem P1S z. B. fast alle zwischen 13. und 19. März.

Jede `.3mf`, die OrcaSlicer oder Bambu Studio exportiert, enthält aber im ZIP-Kopf die Uhrzeit des PCs beim Slicen. Dieser Kopf steht in den ersten 30 Bytes der Datei. Der Tunnel liest deshalb von jeder Datei nur die ersten 64 Bytes, im Hintergrund, wenn sonst nichts zu tun ist. Danach lädt die Liste einmal neu, mit den richtigen Zeiten. Die Zeiten werden in `%TEMP%\orca_lan_storage\job_times.json` gespeichert, ab dem zweiten Öffnen stimmen sie sofort. Dateien ohne diesen Zeitstempel behalten die Zeit des Druckers.

Einschränkung: Das ist der Zeitpunkt des Slicens. Wird eine Datei später direkt am Drucker erneut gedruckt, ändert sich das Datum nicht.

### Während eines Drucks

Für Vorschaubilder, Druckzeit und Gewicht lädt der Speicher-Tab jede `.3mf` komplett herunter. Das belastet einen P1S während des Drucks so stark, dass er minutenlang keine MQTT-Meldungen mehr schickt und Orca ihn als getrennt anzeigt (beobachtet beim Druckstart).

Solange der Drucker druckt (auch Vorbereiten und Pause), lädt der Tunnel deshalb keine Vorschaubilder vom Drucker und liest keine Druckdaten nach. Bereits zwischengespeicherte Vorschauen werden weiter angezeigt, die übrigen kommen nach dem Druck von selbst. Über der Dateiliste steht dann ein Hinweis. Dateiliste, Herunterladen und Löschen funktionieren weiter.

### Hochladen

In der Ansicht *Modell* gibt es den Knopf „Hochladen“ (nur über FTPS, also bei P1/A1 im LAN-Only-Modus). Er lädt eine `.gcode.3mf` vom PC ins Hauptverzeichnis der SD-Karte, dorthin, wo auch Bambu Studio gesendete Dateien ablegt. Gibt es dort schon eine Datei mit dem Namen, fragt Orca vor dem Ersetzen nach.

`PrinterFileSystem` schickt die Datei in Blöcken von 256 KB, ohne auf Antworten zu warten. Der Tunnel puffert höchstens 4 MB davon und reicht sie an ein einziges `STOR` weiter. Danach vergleicht er die Größe in der Dateiliste des Druckers und die MD5-Summe. Schlägt das fehl, wird abgebrochen oder kommen 30 Sekunden lang keine Daten, löscht er die unvollständige Datei wieder, damit sie nicht als druckbarer Auftrag in der Liste steht.

Eine Kopie der hochgeladenen Datei landet gleich im Zwischenspeicher der Vorschauen. Die neu geladene Liste muss die Datei deshalb nicht wieder vom Drucker holen, um Vorschaubild und Datum anzuzeigen.

### Verbindung nach FTPS-Last

Auch ohne Druck schickt ein P1S unter FTPS-Last (Vorschauen laden, Hochladen) mitunter über 30 Sekunden keine MQTT-Meldungen. Beobachtet wurde, dass er danach stumm bleibt, bis die Verbindung neu aufgebaut wird, während er sich per SSDP weiter meldet. Orca verbindet sich bei einem über FTPS angebundenen Drucker deshalb selbst neu (wie bei erneuter Auswahl des Druckers), wenn 30 Sekunden keine Meldung kam, der Drucker per SSDP aber erreichbar ist. Höchstens einmal pro Minute, und nur wenn die Verbindung vorher Meldungen geliefert hat.

## Sortierung

Neben „Aktualisieren“ gibt es eine Auswahl für die Reihenfolge: neueste zuerst (Standard), älteste zuerst, Name A bis Z, Name Z bis A. Die Namenssortierung ignoriert Groß-/Kleinschreibung und Umlaute (ä wie a) und vergleicht Zahlen nach Wert (`plate_2` vor `plate_10`). Die Auswahl gilt für alle Drucker und bleibt gespeichert. Nach Name sortiert ist die Gruppierung nach Jahr/Monat (Zeitraffer) nicht verfügbar.

Da die Anfragen identisch sind, bleibt die übrige Oberfläche (`MediaFilePanel`, `PrinterFileSystem`) unverändert. `MediaFilePanel` wählt den FTPS-Weg nur, wenn der Drucker im LAN-Only-Modus ist **und** keinen lokalen Dateidienst meldet. Drucker, die Orca schon über das Plugin bedienen kann (z. B. X1 im LAN-Modus, alle Drucker im Cloud-Modus), nutzen weiter den bisherigen Weg.

Geänderte Dateien:

- `src/slic3r/GUI/Printer/LanFtpsTunnel.cpp/.hpp` (neu)
- `src/slic3r/GUI/Printer/PrinterFileSystem.cpp/.h`: wählt bei `lanftps://`-URLs den FTPS-Tunnel, Sortierung, Neuladen nach dem Lernen der Zeiten
- `src/slic3r/GUI/MediaFilePanel.cpp/.h`: LAN-Only-Zweig in `fetchUrl()`, Download-Knopf für Modelle, Sortierauswahl
- `src/slic3r/GUI/ImageGrid.cpp/.h`: Datumsbereich nur bei Sortierung nach Zeit, nach oben scrollen bei neuer Sortierung
- `localization/i18n/de/OrcaSlicer_de.po`: deutsche Texte für die Sortierauswahl
- `src/slic3r/CMakeLists.txt`

## Objekte überspringen

Orca enthält den Dialog „Teile überspringen“ aus Bambu Studio. Bei P1/A1 im LAN-Only-Modus blieb der Knopf aber ausgeblendet: Der Drucker meldet die Funktion nicht, und der Dialog holt Vorschaubild und Objektliste über den Dateidienst des Netzwerk-Plugins, den diese Drucker nicht haben.

Jetzt erscheint der Knopf neben Pause/Stopp, sobald der Drucker über FTPS angebunden ist (gleiche Bedingung wie beim Speicher-Tab). Der Dialog liest `Metadata/pick_<platte>.png`, `model_settings.config` und `slice_info.config` aus der `.3mf` des laufenden Drucks auf der SD-Karte. Gesucht wird sie anhand von `gcode_file` und `subtask_name` aus dem Druckerstatus in `/`, `/cache` und `/model`. Das Überspringen selbst ist der MQTT-Befehl `skip_objects`, den der Drucker auch im LAN-Modus annimmt.

Meldet der Drucker die Plattennummer nicht, gilt die einzige Platte mit G-Code in der Datei. Die Druckdatei wird pro Öffnen des Dialogs nur einmal gesucht.

Voraussetzungen: „Objekte beschriften“ (Label objects) war beim Slicen aktiv, und die Teile sind nicht zu einem Objekt zusammengeführt.

Geänderte Dateien: `LanFtpsTunnel.cpp/.hpp` (Anfrage `get_project_file`), `PrinterFileSystem.cpp/.h`, `PartSkipDialog.cpp`, `SkipPartCanvas.cpp/.hpp`, `StatusPanel.cpp`, `DeviceManager.hpp`.

## Stand

- Gegen einen nachgebauten Drucker-FTPS-Server getestet: Dateiliste, Umlaute und Leerzeichen in Dateinamen, Vorschaubilder, Downloads mit MD5-Prüfung, Abbrechen, Löschen, falscher Access Code.
- Am echten P1S getestet: Dateiliste, Vorschaubilder, Druckzeit und Gewicht funktionieren.
- „Drucken“ aus dem Modell-Tab (Orcas Druck-von-SD-Dialog): Das Netzwerk-Plugin schickt den Auftrag auch im LAN-Modus per MQTT (`project_file`). Der Pfad muss `file:///sdcard/<pfad>` lauten, die FTPS-Pfade beginnen aber an der Wurzel der Karte. Ohne Präfix meldet der P1S einen Lese-/Schreibfehler der MicroSD-Karte. Am echten P1S getestet: Mit Präfix startet der Druck.
- Hochladen auf die SD-Karte aus dem Speicher-Tab: gegen den nachgebauten FTPS-Server getestet (5 MB, 60 MB, leere Datei byteidentisch; Abbrechen, falsche MD5, ins Stocken geratener Upload hinterlassen keine Datei; ungültiger Name). Am echten P1S getestet: Hochladen funktioniert. Während und kurz nach dem Hochladen schickt der Drucker etwa 40 Sekunden keine MQTT-Meldungen, danach geht es von selbst weiter (siehe [Verbindung nach FTPS-Last](#verbindung-nach-ftps-last)).
- Objekte überspringen: gegen den nachgebauten FTPS-Server getestet (Suche der Druckdatei, Vorschaubild > 256 KB in Blöcken, MD5, Platten-Fallback, fehlende Datei). Am echten P1S getestet: Überspringen funktioniert, der Drucker bestätigt `skip_objects`.

## Bauen

Windows-Installer: *Actions → Build all → Run workflow*, danach unter *Artifacts* herunterladen.

Die Builds dieses Forks haben eine eigene Versionsnummer (`BambuLocal_VERSION` in `version.inc`, angezeigt unter *Hilfe → Über* und am Anfang des Logs). Die letzte Stelle steigt mit jedem Build, der zum Testen rausgeht, die mittlere mit jedem neuen Pull Request: 0.0.1, 0.0.2, … dann 0.1.1. Artefakte eines Pull Requests heißen z. B. `OrcaSlicer_Windows_V0.0.1_PR2_x64_portable`.

Lokal unter Windows (Visual Studio 2022, CMake, Git):

```
git clone https://github.com/83nn1/OrcaSlicer_BambuLocal.git
cd OrcaSlicer_BambuLocal
build_win.bat
```

---

Original-Projekt: [OrcaSlicer/OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer)
