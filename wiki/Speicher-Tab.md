# Speicher-Tab (SD-Karte) im LAN-Only-Modus

Unter *Gerät → Speicher* zeigt dieser Fork die Dateien auf der SD-Karte von P1- und A1-Druckern auch im LAN-Only-Modus an. Man kann Vorschaubilder, Druckzeit und Gewicht sehen, Dateien herunterladen, löschen, hochladen und direkt von der SD-Karte drucken.

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

Da die Anfragen identisch sind, bleibt die übrige Oberfläche (`MediaFilePanel`, `PrinterFileSystem`) unverändert. `MediaFilePanel` wählt den FTPS-Weg nur, wenn der Drucker im LAN-Only-Modus ist **und** keinen lokalen Dateidienst meldet. Drucker, die Orca schon über das Plugin bedienen kann (z. B. X1 im LAN-Modus, alle Drucker im Cloud-Modus), nutzen weiter den bisherigen Weg.

## Datum der Druckaufträge

Im LAN-Only-Modus hat der Drucker keine verlässliche Uhr: Nach jedem Aus- und Einschalten läuft sie von einem alten Datum weiter. Die Dateizeiten auf der SD-Karte sind dadurch unbrauchbar, bei einem P1S z. B. fast alle zwischen 13. und 19. März.

Jede `.3mf`, die OrcaSlicer oder Bambu Studio exportiert, enthält aber im ZIP-Kopf die Uhrzeit des PCs beim Slicen. Dieser Kopf steht in den ersten 30 Bytes der Datei. Der Tunnel liest deshalb von jeder Datei nur die ersten 64 Bytes, im Hintergrund, wenn sonst nichts zu tun ist. Danach lädt die Liste einmal neu, mit den richtigen Zeiten. Die Zeiten werden in `%TEMP%\orca_lan_storage\job_times.json` gespeichert, ab dem zweiten Öffnen stimmen sie sofort. Dateien ohne diesen Zeitstempel behalten die Zeit des Druckers.

Einschränkung: Das ist der Zeitpunkt des Slicens. Wird eine Datei später direkt am Drucker erneut gedruckt, ändert sich das Datum nicht.

## Während eines Drucks

Für Vorschaubilder, Druckzeit und Gewicht lädt der Speicher-Tab jede `.3mf` komplett herunter. Das belastet einen P1S während des Drucks so stark, dass er minutenlang keine MQTT-Meldungen mehr schickt und Orca ihn als getrennt anzeigt (beobachtet beim Druckstart).

Solange der Drucker druckt (auch Vorbereiten und Pause), lädt der Tunnel deshalb keine Vorschaubilder vom Drucker und liest keine Druckdaten nach. Bereits zwischengespeicherte Vorschauen werden weiter angezeigt, die übrigen kommen nach dem Druck von selbst. Über der Dateiliste steht dann ein Hinweis. Dateiliste, Herunterladen und Löschen funktionieren weiter.

Vorschaubilder, Druckzeit und Gewicht nur aus den nötigen Teilen der `.3mf` zu lesen, statt die ganze Datei zu laden, ist am P1S nicht möglich: Sein FTPS-Server unterstützt kein `REST` (Fortsetzen ab einer Position). Der Speicher-Tab lädt die Datei deshalb weiter komplett.

## Hochladen

In der Ansicht *Modell* gibt es den Knopf „Hochladen“ (nur über FTPS, also bei P1/A1 im LAN-Only-Modus). Er lädt eine `.gcode.3mf` vom PC ins Hauptverzeichnis der SD-Karte, dorthin, wo auch Bambu Studio gesendete Dateien ablegt. Gibt es dort schon eine Datei mit dem Namen, fragt Orca vor dem Ersetzen nach.

`PrinterFileSystem` schickt die Datei in Blöcken von 256 KB, ohne auf Antworten zu warten. Der Tunnel puffert höchstens 4 MB davon und reicht sie an ein einziges `STOR` weiter. Danach vergleicht er die Größe in der Dateiliste des Druckers und die MD5-Summe. Schlägt das fehl, wird abgebrochen oder kommen 30 Sekunden lang keine Daten, löscht er die unvollständige Datei wieder, damit sie nicht als druckbarer Auftrag in der Liste steht.

Eine Kopie der hochgeladenen Datei landet gleich im Zwischenspeicher der Vorschauen. Die neu geladene Liste muss die Datei deshalb nicht wieder vom Drucker holen, um Vorschaubild und Datum anzuzeigen.

## Von der SD-Karte drucken

„Drucken“ in der Ansicht *Modell* öffnet Orcas Druck-von-SD-Dialog. Das Netzwerk-Plugin schickt den Auftrag auch im LAN-Modus per MQTT (`project_file`). Der Pfad muss `file:///sdcard/<pfad>` lauten, die FTPS-Pfade beginnen aber an der Wurzel der Karte. Ohne dieses Präfix meldet der P1S einen Lese-/Schreibfehler der MicroSD-Karte, deshalb setzt der Fork es davor.

## Verbindung nach FTPS-Last

Auch ohne Druck schickt ein P1S unter FTPS-Last (Vorschauen laden, Hochladen) mitunter über 30 Sekunden keine MQTT-Meldungen. Beobachtet wurde, dass er danach stumm bleibt, bis die Verbindung neu aufgebaut wird, während er sich per SSDP weiter meldet. Orca verbindet sich bei einem über FTPS angebundenen Drucker deshalb selbst neu (wie bei erneuter Auswahl des Druckers), wenn 30 Sekunden keine Meldung kam, der Drucker per SSDP aber erreichbar ist. Höchstens einmal pro Minute, und nur wenn die Verbindung vorher Meldungen geliefert hat.

## Sortierung

Neben „Aktualisieren“ gibt es eine Auswahl für die Reihenfolge: neueste zuerst (Standard), älteste zuerst, Name A bis Z, Name Z bis A. Die Namenssortierung ignoriert Groß-/Kleinschreibung und Umlaute (ä wie a) und vergleicht Zahlen nach Wert (`plate_2` vor `plate_10`). Die Auswahl gilt für alle Drucker und bleibt gespeichert. Nach Name sortiert ist die Gruppierung nach Jahr/Monat (Zeitraffer) nicht verfügbar.

## Geänderte Dateien

- `src/slic3r/GUI/Printer/LanFtpsTunnel.cpp/.hpp` (neu)
- `src/slic3r/GUI/Printer/PrinterFileSystem.cpp/.h`: wählt bei `lanftps://`-URLs den FTPS-Tunnel, Sortierung, Neuladen nach dem Lernen der Zeiten
- `src/slic3r/GUI/MediaFilePanel.cpp/.h`: LAN-Only-Zweig in `fetchUrl()`, Download-Knopf für Modelle, Sortierauswahl, `sdcard/`-Präfix beim Drucken
- `src/slic3r/GUI/ImageGrid.cpp/.h`: Datumsbereich nur bei Sortierung nach Zeit, nach oben scrollen bei neuer Sortierung
- `localization/i18n/de/OrcaSlicer_de.po`: deutsche Texte für die Sortierauswahl
- `src/slic3r/CMakeLists.txt`

Siehe auch: [Objekte überspringen](Objekte-überspringen), [Teststand](Teststand).
