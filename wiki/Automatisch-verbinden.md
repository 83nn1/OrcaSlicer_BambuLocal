# Automatisch mit dem letzten Drucker verbinden

Im Cloud-Modus verbindet sich Orca beim Start von selbst mit dem zuletzt benutzten Drucker. Im LAN-Only-Modus musste man den Drucker bisher nach jedem Neustart von Hand auswählen.

## Problem

`load_last_machine()` sah nur in der Liste der Cloud-Drucker nach, die im LAN-Only-Modus leer ist, und lief erst, wenn der Gerät-Tab oder ein Druckdialog geöffnet wurde. Außerdem tauscht Orca bei jedem Start den Drucker-Agenten aus und hat dabei den zuletzt gewählten Drucker vergessen.

## Lösung

- `load_last_machine()` akzeptiert auch einen lokal gekoppelten Drucker mit Access Code.
- Orca wählt den zuletzt benutzten LAN-Drucker einmal aus, sobald er sich nach dem Start (oder nach einem Agentenwechsel) zum ersten Mal per SSDP im Netzwerk meldet.
- Der zuletzt gewählte Drucker bleibt beim Agentenwechsel erhalten.

## Einstellung

*Einstellungen → Online → Bambu network plug-in → „Connect to the last used printer on startup“* (standardmäßig an).
