# Benachrichtigungen

Im Cloud-Modus meldet die Bambu-Handy-App, wenn ein Druck fertig ist oder hängt. Im LAN-Only-Modus gibt es diese Meldung nicht, man muss selbst nachsehen.

## Lösung im Fork

Orca zeigt eine Desktop-Benachrichtigung (unter Windows unten rechts), wenn ein Druck auf einem verbundenen Drucker

- fertig ist (`gcode_state` wechselt auf `FINISH`),
- abgebrochen wurde oder fehlgeschlagen ist (`FAILED`),
- pausiert (`PAUSE`), z. B. weil das Filament ausgegangen ist oder der Drucker einen Fehler meldet.

Die Meldung nennt den Drucker und den Namen des Druckauftrags. Ein Klick darauf holt Orca nach vorn und öffnet den Tab *Gerät* mit diesem Drucker.

Der Status kommt per MQTT, wie die übrige Statusanzeige. Gemeldet wird nur ein echter Wechsel aus einem laufenden Druck (`PREPARE`, `RUNNING`, bei Fertig/Abbruch auch `PAUSE`). Die erste Meldung nach dem Verbinden zählt nicht, deshalb kommt beim Start von Orca keine Benachrichtigung für einen längst beendeten Druck. Orca muss dafür laufen; ist Orca geschlossen oder die Verbindung gerade getrennt, geht die Meldung verloren.

Die Funktion gilt für alle Bambu-Drucker, im LAN- und im Cloud-Modus.

## Einstellung

*Einstellungen → Online → Bambu network plug-in → „Notify when a print finishes, stops or pauses“* (standardmäßig an).

Im Log steht bei jeder Benachrichtigung eine Zeile `BambuLocal: print state RUNNING -> FINISH, showing notification`.
