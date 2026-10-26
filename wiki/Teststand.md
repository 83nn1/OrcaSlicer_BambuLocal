# Teststand

Was getestet wurde, gegen einen nachgebauten Drucker-FTPS-Server und am echten Bambu Lab P1S (LAN-Only, mit AMS).

| Funktion | Nachgebauter FTPS-Server | Echter P1S |
|---|---|---|
| Dateiliste, Vorschaubilder, Druckzeit, Gewicht | Umlaute und Leerzeichen in Dateinamen, Downloads mit MD5-Prüfung, Abbrechen, Löschen, falscher Access Code | funktioniert |
| Datum aus dem Kopf der `.3mf` | | stimmt, Liste richtig sortiert |
| Von der SD-Karte drucken | | Druck startet (mit `sdcard/`-Präfix) |
| Hochladen auf die SD-Karte | 5 MB, 60 MB, leere Datei byteidentisch; Abbrechen, falsche MD5, ins Stocken geratener Upload hinterlassen keine Datei; ungültiger Name | funktioniert |
| Objekte überspringen | Suche der Druckdatei, Vorschaubild > 256 KB in Blöcken, MD5, Platten-Fallback, fehlende Datei | Drucker bestätigt `skip_objects`, meldet übersprungene Objekte in `s_obj` |

## Bekannte Eigenheiten des P1S

- Während und kurz nach dem Hochladen schickt der Drucker etwa 40 Sekunden keine MQTT-Meldungen, danach geht es von selbst weiter (siehe [Verbindung nach FTPS-Last](Speicher-Tab#verbindung-nach-ftps-last)).
- Sein FTPS-Server unterstützt kein `REST`, Dateien können nur komplett geladen werden.
- Ohne Cloud läuft seine Uhr nach jedem Einschalten von einem alten Datum weiter (siehe [Datum der Druckaufträge](Speicher-Tab#datum-der-druckaufträge)).
