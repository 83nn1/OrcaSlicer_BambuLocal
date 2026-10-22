# OrcaSlicer_BambuLocal

Fork von [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer) mit einer einzigen Änderung: **Zugriff auf den Speicher (SD-Karte) von Bambu-Lab-Druckern im LAN-Only-Modus.**

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

Da die Anfragen identisch sind, bleibt die übrige Oberfläche (`MediaFilePanel`, `PrinterFileSystem`) unverändert. `MediaFilePanel` wählt den FTPS-Weg nur, wenn der Drucker im LAN-Only-Modus ist **und** keinen lokalen Dateidienst meldet. Drucker, die Orca schon über das Plugin bedienen kann (z. B. X1 im LAN-Modus, alle Drucker im Cloud-Modus), nutzen weiter den bisherigen Weg.

Geänderte Dateien:

- `src/slic3r/GUI/Printer/LanFtpsTunnel.cpp/.hpp` (neu)
- `src/slic3r/GUI/Printer/PrinterFileSystem.cpp/.h`: wählt bei `lanftps://`-URLs den FTPS-Tunnel
- `src/slic3r/GUI/MediaFilePanel.cpp/.h`: LAN-Only-Zweig in `fetchUrl()`, Download-Knopf für Modelle
- `src/slic3r/CMakeLists.txt`

## Stand

- Gegen einen nachgebauten Drucker-FTPS-Server getestet: Dateiliste, Umlaute und Leerzeichen in Dateinamen, Vorschaubilder, Downloads mit MD5-Prüfung, Abbrechen, Löschen, falscher Access Code.
- Am echten P1S noch nicht getestet.
- Ob „Drucken“ aus dem Modell-Tab (Orcas Druck-von-SD-Dialog) im LAN-Modus durchläuft, ist offen.
- Hochladen auf die SD-Karte aus dem Speicher-Tab ist nicht umgesetzt.

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
