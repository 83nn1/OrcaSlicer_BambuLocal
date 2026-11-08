# Automatisch mit dem letzten Drucker verbinden

Orca verbindet sich beim Start mit dem zuletzt benutzten Drucker. Seit OrcaSlicer 2.5.0 macht das Original das selbst (`DeviceManager::restore_last_machine()`), für LAN- und Cloud-Drucker, aber ohne Einstellung. Einen LAN-Drucker findet es dabei nur, wenn er beim Start schon im Netzwerk gesehen wurde.

## Lösung im Fork

- Orca wählt den zuletzt benutzten LAN-Drucker einmal aus, sobald er sich nach dem Start (oder nach einem Agentenwechsel) zum ersten Mal per SSDP im Netzwerk meldet. Ist schon ein Drucker gewählt, passiert nichts, es gibt also keine doppelte Verbindung.
- Eine Einstellung schaltet beides ab: das Verbinden beim SSDP-Signal und das Wiederherstellen beim Start aus dem Original.
- Öffnet man den Gerät-Tab oder einen Druckdialog, wählt Orca den zuletzt benutzten Drucker wie im Original auch bei ausgeschalteter Einstellung.

## Einstellung

*Einstellungen → Online → Bambu network plug-in → „Connect to the last used printer on startup“* (standardmäßig an).
