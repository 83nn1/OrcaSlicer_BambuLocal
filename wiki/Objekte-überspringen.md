# Objekte überspringen im LAN-Only-Modus

Während eines Drucks einzelne Objekte abbrechen, z. B. wenn sich ein Teil vom Druckbett gelöst hat, während die übrigen weiterlaufen.

## Problem

Orca enthält den Dialog „Teile überspringen“ aus Bambu Studio. Bei P1/A1 im LAN-Only-Modus blieb der Knopf aber ausgeblendet: Der Drucker meldet die Funktion nicht, und der Dialog holt Vorschaubild und Objektliste über den Dateidienst des Netzwerk-Plugins, den diese Drucker nicht haben.

## Lösung

Der Knopf erscheint neben Pause/Stopp, sobald der Drucker über FTPS angebunden ist (gleiche Bedingung wie beim [Speicher-Tab](Speicher-Tab)). Der Dialog liest `Metadata/pick_<platte>.png`, `model_settings.config` und `slice_info.config` aus der `.3mf` des laufenden Drucks auf der SD-Karte. Gesucht wird sie anhand von `gcode_file` und `subtask_name` aus dem Druckerstatus in `/`, `/cache` und `/model`. Das Überspringen selbst ist der MQTT-Befehl `skip_objects`, den der Drucker auch im LAN-Modus annimmt. Der Drucker meldet die übersprungenen Objekte etwa 2 Sekunden später in `s_obj` zurück, der Zähler am Knopf stimmt damit.

Meldet der Drucker die Plattennummer nicht, gilt die einzige Platte mit G-Code in der Datei. Die Druckdatei wird pro Öffnen des Dialogs nur einmal gesucht.

## Voraussetzungen

- „Objekte beschriften“ (Label objects) war beim Slicen aktiv.
- Die Teile sind nicht zu einem Objekt zusammengeführt.

## Geänderte Dateien

`LanFtpsTunnel.cpp/.hpp` (Anfrage `get_project_file`), `PrinterFileSystem.cpp/.h`, `PartSkipDialog.cpp`, `SkipPartCanvas.cpp/.hpp`, `StatusPanel.cpp`, `DeviceManager.hpp`.
