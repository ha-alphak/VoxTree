# Projektstatus

**Berichtsdatum:** 25. Juli 2026  
**Phase:** Projektfundament abgeschlossen
**Gesamtstatus:** Grün

## Abgeschlossen

- Ursprüngliche Feature-Spezifikation analysiert.
- Produkt als eigenständige Voice-Communication-Anwendung festgelegt.
- Windows-Clienttechnologie festgelegt.
- Linux-Serverplattform festgelegt.
- LiveKit als primärer Voice-Transport ausgewählt.
- Mumble als Rückfalloption bestimmt.
- Grundlegendes Sicherheits- und Routingmodell festgelegt.
- Separate PTT-Aktionen und Joystick-Unterstützung festgelegt.
- Initiale Sprecherlimits definiert.
- Integrationsfähigkeit als Architekturziel aufgenommen.
- Versionierter Dokumentationsordner angelegt.
- CMake-Projekt mit Windows-, Debian-, Debug-, Release- und Analyse-Presets
  angelegt.
- MSVC-Warnprofil, Sanitizer-Konfiguration, clang-format und clang-tidy
  eingerichtet.
- Minimale C++20-Foundation-Bibliothek mit installierbarem CMake-Paket
  angelegt.
- CTest-Testfundament eingerichtet.
- GitHub-CI für Windows Server 2022 und Debian 13.6 eingerichtet.
- Lokale MSVC-Debug- und Release-Builds erfolgreich ausgeführt.
- Lokale Tests, Formatprüfung, statische Analyse und Paketinstallation
  erfolgreich validiert.

## Aktueller Stand

- Das technische Projektfundament ist vorhanden und lokal validiert.
- Es existiert noch kein fachlicher Anwendungs- oder Servercode.
- Das Projektfundament benötigt keine externen C++-Abhängigkeiten.
- Das LiveKit-C++-Quality-Gate wurde noch nicht begonnen.
- Die Spezifikation liegt unverändert im Projekt vor.

## Nächster Schritt

Transportunabhängigen Domänenkern mit starken ID-Typen, Hierarchie,
Membership-Snapshots, Rollenrechten und deterministischer Empfängerermittlung
anlegen.

## Validierung

| Prüfung | Ergebnis |
|---|---|
| Windows MSVC Debug | Bestanden |
| Windows MSVC Release | Bestanden |
| CTest Debug und Release | 1 von 1 Tests bestanden |
| clang-format | Bestanden |
| clang-tidy | Bestanden |
| CMake-Installation und Paketexport | Bestanden |
| Debian-CI | Konfiguriert, Ausführung nach Push |

## Risiken

| Risiko | Status | Maßnahme |
|---|---|---|
| Reife und Integration des LiveKit-C++-SDK | Offen | Frühes Quality Gate und Transportabstraktion |
| Autoritative Isolation im Shared-Room-Modell | Offen | Zunächst getrennte Räume verwenden |
| Hintergrund-PTT für unterschiedliche HID-Geräte | Offen | Früher Hardware-Prototyp mit Raw Input |
| Windows 10 außerhalb des regulären Supports | Akzeptiert | Build 19045 unterstützen und Windows 11 mittesten |
| 200-Spieler-Skalierung | Offen | Früher Lasttest vor UI-Vollausbau |

