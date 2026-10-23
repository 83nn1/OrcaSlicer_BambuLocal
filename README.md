# OrcaSlicer_BambuLocal

Fork von [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer) für Bambu-Lab-Drucker im LAN-Only-Modus (P1/A1):

- **Zugriff auf den Speicher (SD-Karte)** unter *Gerät → Speicher*
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
| Datum der Druckaufträge | Zeitstempel im Kopf der `.3mf` (siehe unten) |

### Datum der Druckaufträge

Im LAN-Only-Modus hat der Drucker keine verlässliche Uhr: Nach jedem Aus- und Einschalten läuft sie von einem alten Datum weiter. Die Dateizeiten auf der SD-Karte sind dadurch unbrauchbar, bei einem P1S z. B. fast alle zwischen 13. und 19. März.

Jede `.3mf`, die OrcaSlicer oder Bambu Studio exportiert, enthält aber im ZIP-Kopf die Uhrzeit des PCs beim Slicen. Dieser Kopf steht in den ersten 30 Bytes der Datei. Der Tunnel liest deshalb von jeder Datei nur die ersten 64 Bytes, im Hintergrund, wenn sonst nichts zu tun ist. Danach lädt die Liste einmal neu, mit den richtigen Zeiten. Die Zeiten werden in `%TEMP%\orca_lan_storage\job_times.json` gespeichert, ab dem zweiten Öffnen stimmen sie sofort. Dateien ohne diesen Zeitstempel behalten die Zeit des Druckers.

Einschränkung: Das ist der Zeitpunkt des Slicens. Wird eine Datei später direkt am Drucker erneut gedruckt, ändert sich das Datum nicht.

### Während eines Drucks

Für Vorschaubilder, Druckzeit und Gewicht lädt der Speicher-Tab jede `.3mf` komplett herunter. Das belastet einen P1S während des Drucks so stark, dass er minutenlang keine MQTT-Meldungen mehr schickt und Orca ihn als getrennt anzeigt (beobachtet beim Druckstart).

Solange der Drucker druckt (auch Vorbereiten und Pause), lädt der Tunnel deshalb keine Vorschaubilder vom Drucker und liest keine Druckdaten nach. Bereits zwischengespeicherte Vorschauen werden weiter angezeigt, die übrigen kommen nach dem Druck von selbst. Über der Dateiliste steht dann ein Hinweis. Dateiliste, Herunterladen und Löschen funktionieren weiter.

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
- Ob „Drucken“ aus dem Modell-Tab (Orcas Druck-von-SD-Dialog) im LAN-Modus durchläuft, ist offen.
- Hochladen auf die SD-Karte aus dem Speicher-Tab ist nicht umgesetzt.
- Objekte überspringen: gegen den nachgebauten FTPS-Server getestet (Suche der Druckdatei, Vorschaubild > 256 KB in Blöcken, MD5, Platten-Fallback, fehlende Datei). Am echten P1S getestet: Überspringen funktioniert, der Drucker bestätigt `skip_objects`.

## Bauen

Windows-Installer: *Actions → Build all → Run workflow*, danach unter *Artifacts* herunterladen.

Lokal unter Windows (Visual Studio 2022, CMake, Git):

```
git clone https://github.com/83nn1/OrcaSlicer_BambuLocal.git
cd OrcaSlicer_BambuLocal
build_win.bat
```

---

Original-Projekt: [OrcaSlicer/OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer)
