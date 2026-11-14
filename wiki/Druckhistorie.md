# Druckhistorie

Im Cloud-Modus führt Bambu Lab eine Liste aller Drucke. Ohne Cloud gibt es diese Liste nicht, und die Uhr des Druckers geht im LAN-Only-Modus falsch. Man sieht also nicht, wann eine Datei zuletzt gedruckt wurde und ob der Druck geklappt hat.

## Lösung im Fork

Orca merkt sich jeden Druck, dessen Ende es mitbekommt, mit der Uhr des PCs:

- Datei des Druckauftrags (Name aus `gcode_file`, sonst der Auftragsname `subtask_name`),
- Start, wenn Orca den Druckstart gesehen hat,
- Ende,
- Ergebnis: fertig (`FINISH`) oder abgebrochen/fehlgeschlagen (`FAILED`).

Im **Speicher-Tab** steht dann oben auf dem Vorschaubild der Datei ein Hinweis zum letzten Druck:

- grün „Printed 14.11.2026 17:30, took 2h15m“, wenn er fertig wurde (die Dauer nur, wenn Orca auch den Start gesehen hat),
- rot „Stopped 14.11.2026 17:30“, wenn er abgebrochen wurde oder fehlgeschlagen ist.

Dateien, die mit Orca noch nie gedruckt wurden, bleiben unverändert.

Die Liste liegt in `print_history.json` im Datenordner von Orca (unter Windows `%APPDATA%\OrcaSlicer`) und hält die letzten 2000 Drucke. Sie gilt pro Drucker. Ist Orca beim Druckende geschlossen oder die Verbindung getrennt, fehlt der Druck in der Liste.

Im Log steht pro Druck eine Zeile `BambuLocal: print history: <Datei> FINISH, <Sekunden> s`.
