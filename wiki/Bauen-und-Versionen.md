# Bauen, Versionen und Releases

## Installer über GitHub Actions

*Actions → Build all → Run workflow*, danach unter *Artifacts* herunterladen. Jeder Lauf baut Windows (x64, ARM64), macOS (Universal-DMG aus arm64 und x86_64) und Linux (AppImage und Flatpak für x86_64 und aarch64). Die macOS-App ist nicht signiert und nicht notarisiert, weil das ein Apple-Entwicklerkonto braucht; das Original signiert nur in seinem eigenen Repo.

## Versionsnummern

Die Builds dieses Forks haben eine eigene Versionsnummer (`BambuLocal_VERSION` in `version.inc`). Startfenster, *Hilfe → Über* und der Anfang des Logs zeigen sie als `BambuLocal V0.7.2`, bei Builds aus einem Pull Request mit dessen Nummer: `BambuLocal V0.7.2 (PR #14)`. Die PR-Nummer setzt der Build-Workflow über die Umgebungsvariable `bambulocal_pr`. Die letzte Stelle steigt mit jedem Build, der zum Testen rausgeht, die mittlere mit jedem neuen Pull Request: 0.0.1, 0.0.2, … dann 0.1.1. Artefakte eines Pull Requests heißen z. B. `OrcaSlicer_Windows_V0.0.1_PR2_x64_portable`. Pull Requests, die nur README oder Wiki ändern, starten keinen Build und bekommen deshalb keine neue Nummer.

## Releases

Ein Release entsteht aus einem erfolgreichen *Build all*-Lauf auf `main`: *Actions → BambuLocal release → Run workflow* mit dessen Run-ID und der Version aus `version.inc`. Builds aus Pull Requests werden nicht veröffentlicht.

Die Dateinamen im Release sind immer gleich (`OrcaSlicer_BambuLocal_Windows_{x64,arm64}_{Installer.exe,portable.zip}`, `OrcaSlicer_BambuLocal_macOS_universal.dmg`, `OrcaSlicer_BambuLocal_Linux_{x86_64,aarch64}.AppImage`), die Download-Links im README zeigen deshalb immer auf das neueste Release.

## Abgleich mit OrcaSlicer

Der Fork übernimmt neue Versionen des Originals per Merge des jeweiligen Release-Tags (Remote `upstream` = `OrcaSlicer/OrcaSlicer`), ohne Force-Push. Aktuelle Basis ist OrcaSlicer 2.5.0 (V0.7.x).

OrcaSlicer 2.5.0 bringt ein Python-Plugin-System. Die Funktionen dieses Forks lassen sich damit nicht als Plugin umsetzen: Plugins kommen weder an die Druckerverbindung des Geräte-Tabs noch an den Speicher-Tab oder den Dialog „Teile überspringen“, und das automatische Verbinden braucht Änderungen im Kern. Der Fork bleibt deshalb ein Fork.

## Lokal bauen unter Windows

Voraussetzungen: Visual Studio 2022, CMake, Git.

```
git clone https://github.com/83nn1/OrcaSlicer_BambuLocal.git
cd OrcaSlicer_BambuLocal
build_win.bat
```

## Wiki

Die Seiten dieses Wikis liegen im Ordner `wiki/` des Repos. Nach einem Merge auf `main` überträgt sie der Workflow *Wiki sync* hierher. Änderungen bitte dort machen, direkte Änderungen im Wiki werden beim nächsten Sync überschrieben.
