# Bauen, Versionen und Releases

## Windows-Installer über GitHub Actions

*Actions → Build all → Run workflow*, danach unter *Artifacts* herunterladen.

## Versionsnummern

Die Builds dieses Forks haben eine eigene Versionsnummer (`BambuLocal_VERSION` in `version.inc`). Startfenster, *Hilfe → Über* und der Anfang des Logs zeigen sie als `BambuLocal V0.5.3`, bei Builds aus einem Pull Request mit dessen Nummer: `BambuLocal V0.5.3 (PR #10)`. Die PR-Nummer setzt der Build-Workflow über die Umgebungsvariable `bambulocal_pr`. Die letzte Stelle steigt mit jedem Build, der zum Testen rausgeht, die mittlere mit jedem neuen Pull Request: 0.0.1, 0.0.2, … dann 0.1.1. Artefakte eines Pull Requests heißen z. B. `OrcaSlicer_Windows_V0.0.1_PR2_x64_portable`.

## Releases

Ein Release entsteht aus einem erfolgreichen *Build all*-Lauf auf `main`: *Actions → BambuLocal release → Run workflow* mit dessen Run-ID und der Version aus `version.inc`. Builds aus Pull Requests werden nicht veröffentlicht.

Die Dateinamen im Release sind immer gleich (`OrcaSlicer_BambuLocal_Windows_{x64,arm64}_{Installer.exe,portable.zip}`), die Download-Links im README zeigen deshalb immer auf das neueste Release.

## Lokal bauen unter Windows

Voraussetzungen: Visual Studio 2022, CMake, Git.

```
git clone https://github.com/83nn1/OrcaSlicer_BambuLocal.git
cd OrcaSlicer_BambuLocal
build_win.bat
```

## Wiki

Die Seiten dieses Wikis liegen im Ordner `wiki/` des Repos. Nach einem Merge auf `main` überträgt sie der Workflow *Wiki sync* hierher. Änderungen bitte dort machen, direkte Änderungen im Wiki werden beim nächsten Sync überschrieben.
