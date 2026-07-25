# Projektstatus

**Berichtsdatum:** 25. Juli 2026  
**Phase:** Domänenkern – Zustandsautomaten abgeschlossen<br>
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
- Transportunabhängige `hvc-domain`-Bibliothek mit starken Typen für Spieler-,
  Gruppen-, Spezialisierungs-, Team-, Hierarchie-, Rollen- und
  Transmission-IDs angelegt.
- Datengetriebene und validierte Gruppen-, Spezialisierungs- und
  Team-Hierarchie implementiert.
- Immutable, versionierte Membership-Snapshots mit konsistenter
  Hierarchiepfadprüfung implementiert.
- Rollenrechte für Senden und Empfangen unabhängig voneinander modelliert.
- Deterministische Empfängerermittlung für Team, Specialization und Group
  einschließlich Isolation, Verbindungsstatus, Voice-Ban sowie lokaler
  Mute-/Block-Restriktionen implementiert.
- Routing- und Validierungstests einschließlich eines deterministischen
  200-Spieler-Szenarios ergänzt.
- Transportunabhängige Zustandsautomaten für Verbindung, Reconnect und
  Transmission ergänzt.
- Membership-Refresh und Wiederherstellung der Empfangsabonnements als
  Voraussetzung für einen bereiten Verbindungszustand umgesetzt.
- Sicheren Transmissionsabbruch bei Disconnect, Membership-Änderung,
  Rechteentzug, Timeout und Transportfehler umgesetzt.
- Korrelierte Transmission-Anfragen verhindern, dass verspätete Antworten eine
  neue oder nach einem Reconnect verworfene Transmission aktivieren.

## Aktueller Stand

- Das technische Projektfundament und der transportunabhängige Domänenkern für
  Hierarchie, Routing und Sitzungszustände sind vorhanden und lokal validiert.
- Es existiert noch kein Anwendungs-, Control-Plane- oder Transportcode.
- Das Projektfundament benötigt keine externen C++-Abhängigkeiten.
- Das LiveKit-C++-Quality-Gate wurde noch nicht begonnen.
- Die Spezifikation liegt unverändert im Projekt vor.

## Nächster Schritt

Grundgerüst der Linux-Control-Plane anlegen und die ersten
Anwendungs-Schnittstellen für authentifizierte Sessions, autoritative
Membership-Snapshots und serverseitig autorisierte Transmissionen definieren.

## Validierung

| Prüfung | Ergebnis |
|---|---|
| Windows MSVC Debug | Bestanden |
| Windows MSVC Release | Bestanden |
| CTest Debug und Release | 3 von 3 Tests bestanden |
| Reconnect ohne automatische Transmission | Bestanden |
| clang-format | Bestanden |
| clang-tidy | Bestanden |
| CMake-Installation und Paketexport inklusive `hvc::domain` | Bestanden |
| Debian-CI | Konfiguriert, Ausführung nach Push |

## Risiken

| Risiko | Status | Maßnahme |
|---|---|---|
| Reife und Integration des LiveKit-C++-SDK | Offen | Frühes Quality Gate und Transportabstraktion |
| Autoritative Isolation im Shared-Room-Modell | Offen | Zunächst getrennte Räume verwenden |
| Hintergrund-PTT für unterschiedliche HID-Geräte | Offen | Früher Hardware-Prototyp mit Raw Input |
| Windows 10 außerhalb des regulären Supports | Akzeptiert | Build 19045 unterstützen und Windows 11 mittesten |
| 200-Spieler-Skalierung | Offen | Früher Lasttest vor UI-Vollausbau |
