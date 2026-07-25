# Projektstatus

**Berichtsdatum:** 25. Juli 2026  
**Phase:** Linux-Control-Plane – Persistenz und Schema-Migrationen<br>
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
- Linux-Control-Plane-Executable und transportunabhängige
  `hvc-application`-Bibliothek angelegt.
- Schnittstellen für Session-Authentifizierung, Session-Ablage und
  autoritative Membership-Kontexte definiert.
- Serverseitige Start-Autorisierung implementiert, die Senderidentität aus der
  Session ableitet und keine clientseitige Empfängerliste akzeptiert.
- Gerätebindung, Session-Ablauf, Membership-Version und Rollenrechte bei der
  Start-Autorisierung berücksichtigt.
- Korrelations-, Session- und Geräte-IDs als starke Typen ergänzt.
- Anwendungstests für gültige, unbekannte, gerätefremde, abgelaufene, veraltete
  und nicht autorisierte Anfragen ergänzt.
- Threadsicheren In-Memory-Store für Sessions, autoritative
  Membership-Kontexte und aktive Transmissionen implementiert.
- Vollständige Start- und End-Anwendungsfälle mit Geräte- und
  Session-Ownership sowie genau einer aktiven Transmission pro Spieler
  umgesetzt.
- Race zwischen Autorisierung und Aktivierung durch atomare erneute Prüfung von
  Session und Membership-Version geschlossen.
- Membership- und Rechteänderungen mit höherer Version sowie Session-Entfernung
  beenden aktive Transmissionen atomar mit nachvollziehbarem Abbruchgrund.
- Anwendungstests für Start, Ende, fremde Endanforderungen, parallelen
  Startkonflikt, veraltete Updates und atomare Abbrüche ergänzt.
- Konfigurierbare Sliding-Window-Rate-Limits für Start- und Endanforderungen
  getrennt pro authentifiziertem Spieler umgesetzt.
- Konfigurierbare Transmission-Maximaldauer und atomare Timeout-Prüfung mit
  korrelierten Abbruchergebnissen ergänzt.
- Frameworkunabhängigen Moderationsabbruch mit eigener
  Autorisierungsschnittstelle, Session- und Geräteprüfung sowie atomarer
  Beendigung implementiert.
- Anwendungstests für Rate-Limit-Grenzen und -Fehler, Timeout-Grenzzeitpunkte
  sowie autorisierte und nicht autorisierte Moderationsabbrüche ergänzt.
- Frameworkunabhängige, typisierte Audit-Event-Schnittstelle für erfolgreiche
  Starts, reguläre Enden, Ablehnungen und erzwungene Abbrüche ergänzt.
- Audit-Events für Moderation, Timeout, Session-Entfernung sowie Membership- und
  Rechteänderungen angebunden; interne Empfängerlisten bleiben ausgeschlossen.
- Anwendungstests für Eventtyp, Operation, Akteur, Korrelation, Ablehnungs- und
  Abbruchgründe ergänzt.
- Schreibbare Session-Repository-Schnittstelle ergänzend zur lesenden
  Anwendungsschnittstelle festgelegt.
- Plattformübergreifenden SQLite-Adapter für dauerhafte, gerätegebundene
  Sessions implementiert.
- Transaktionale, strikt aufsteigende Schema-Migrationen mit Migrationshistorie
  und Schutz vor nicht unterstützten neueren Schemaversionen eingerichtet.
- Control-Plane-Start an die Datenbankinitialisierung und Migration angebunden;
  Datenbankpfad ist per Kommandozeile konfigurierbar.
- Persistenztests für initiale und idempotente Migration, Neustart,
  Aktualisierung und Löschung von Sessions ergänzt.

## Aktueller Stand

- Das technische Projektfundament, der transportunabhängige Domänenkern, der
  In-Memory-Transmissionslebenszyklus und die erste dauerhafte Session-Ablage
  sind vorhanden und lokal validiert.
- Es existieren noch keine Netzwerk-, Membership-Persistenz-, Audit-Persistenz-
  oder Voice-Transportadapter.
- SQLite wird unter Windows über `winsqlite3` aus dem Windows SDK und unter
  Debian über das Systempaket `libsqlite3-dev` angebunden.
- Das LiveKit-C++-Quality-Gate wurde noch nicht begonnen.
- Die Spezifikation liegt unverändert im Projekt vor.

## Nächster Schritt

SQLite-Persistenz auf autoritative Membership-Kontexte und Rollenrichtlinien
erweitern und dabei atomare Versionsaktualisierungen mit dem bestehenden
Transmissionsabbruch verbinden.

## Validierung

| Prüfung | Ergebnis |
|---|---|
| Windows MSVC Debug | Bestanden |
| Windows MSVC Release | Bestanden |
| CTest Debug | 6 von 6 Tests bestanden |
| CTest Release | 6 von 6 Tests bestanden |
| Reconnect ohne automatische Transmission | Bestanden |
| clang-format | Bestanden |
| clang-tidy | Konfiguriert, aktueller Lauf über Debian-CI |
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
