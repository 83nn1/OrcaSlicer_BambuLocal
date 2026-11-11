# Druckermodelle

Der Fork prüft nirgends das Druckermodell. Welcher Weg genutzt wird, entscheidet allein, was der Drucker im LAN-Only-Modus über MQTT meldet.

## Zwei Arten von Druckern

**Ohne lokalen Dateidienst (P1P, P1S, A1, A1 mini):** Der Drucker meldet unter `ipcam.file.local` keinen Dateidienst. Orca kann den Speicher dann gar nicht öffnen und blendet „Objekte überspringen“ aus. Hier springt der Fork ein: Speicher-Tab und Objekte überspringen laufen über FTPS (siehe [Speicher-Tab](Speicher-Tab), [Objekte überspringen](Objekte-überspringen)).

**Mit lokalem Dateidienst (X1-Serie, X2D, H2-Serie wie H2D und H2C):** Diese Drucker bieten im LAN-Modus selbst einen Dateidienst an (Port 6000), den Orca wie Bambu Studio über das Bambu-Netzwerk-Plugin nutzt. Speicher-Tab, Drucken vom Speicher und Objekte überspringen laufen damit schon im Original-OrcaSlicer. Der Fork lässt diesen Weg unverändert.

Kommt der Dateidienst eines solchen Druckers nicht zustande (z. B. weil Plugin und Firmware nicht zusammenpassen), weicht der Fork auf FTPS aus, das alle Bambu-Drucker im LAN-Modus anbieten (Port 990, Benutzer `bblp`, Access Code):

- **Speicher-Tab:** Nach dem ersten fehlgeschlagenen Verbindungsversuch lädt er die Liste per FTPS, bis ein anderer Drucker gewählt wird. Damit stehen dann auch Hochladen, das Datum aus dem `.3mf`-Kopf und die Pause der Vorschaubilder während des Drucks zur Verfügung. Im Log steht `falling back to FTPS`.
- **Objekte überspringen:** Der Dialog liest die Dateien des laufenden Drucks dann per FTPS aus seiner `.3mf`. Den Knopf zeigt Orca bei diesen Druckern, wenn die Firmware die Funktion meldet.

## Was überall gleich ist

- [Automatisch verbinden](Automatisch-verbinden) nutzt die SSDP-Meldung des Druckers und funktioniert mit jedem LAN-Drucker.
- Sortierung im Speicher-Tab (neueste zuerst, Name …) gilt für beide Wege.
- Das Halten der Verbindung (`get_version` nach 15 s Stille) und die Kopie des gesendeten Auftrags gelten nur für Drucker ohne Dateidienst, weil nur dort der Fork die Verbindung selbst übernimmt.

## H3C

Den H3C (Werkzeugwechsler mit neun Köpfen, erschienen im November 2026) kennt OrcaSlicer noch nicht, auch nicht in der aktuellen Entwicklungsversion: Es gibt weder ein Druckerprofil noch eine Gerätebeschreibung. Sobald das Original ihn unterstützt und der Fork diesen Stand übernimmt, gelten für ihn dieselben Regeln wie oben, weil der Fork nicht nach Modell, sondern nach den Meldungen des Druckers entscheidet.

## Getestet

Getestet ist nur der P1S (siehe [Teststand](Teststand)). X1C, X2D und H2C sind nicht an echter Hardware geprüft. Ungeklärt ist dort vor allem, ob beim FTPS-Ausweichweg „Drucken vom Speicher“ mit dem Pfad `sdcard/<datei>` startet (H2C und X2D haben zusätzlich einen internen Speicher) und welche Ordner die Drucker per FTPS zeigen. Rückmeldungen mit Log (`%APPDATA%\OrcaSlicer\log`, Suche nach `LanFtps` und `MediaFilePanel`) helfen.
