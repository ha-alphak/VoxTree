# Projektstatus

**Berichtsdatum:** 25. Juli 2026  
**Phase:** Linux-Control-Plane – versionierter HTTP-Netzwerkadapter<br>
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
- Vollständigen GitHub-CI-Lauf mit Windows MSVC Debug/Release, Debian GCC
  Debug/Release und Debian Clang-Analyse erfolgreich abgeschlossen.
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
- Schreibbare Anwendungsschnittstelle für autoritative Membership-Kontexte mit
  atomarem Compare-and-Replace auf strikt höhere Versionen ergänzt.
- SQLite-Schema auf normalisierte Hierarchien, Scopes, Memberships,
  Rollenzuweisungen und getrennte Sende-/Empfangsrechte erweitert.
- Vollständige Membership-Snapshots und Rollenrichtlinien über Prozessneustarts
  hinweg rekonstruierbar gemacht.
- Persistentes Membership-Update im `InMemoryControlPlaneStore` mit dem
  bestehenden atomaren Transmissionsabbruch verbunden.
- Persistenztests für Kontext-Neustart, Versionsschutz, Löschung sowie
  gekoppelten Rechtewechsel und Transmissionsabbruch ergänzt.
- Strukturierte Transmission-Audit-Events mit monotoner Sequenz vollständig in
  SQLite persistierbar gemacht.
- Sequenzbasierte, begrenzte Abfrage in stabiler Einfügereihenfolge und
  zeitbasierte Löschung in begrenzten Batches ergänzt.
- Audit-Schema auf Empfängeranzahl beschränkt; interne Empfänger-IDs werden
  weder gespeichert noch über die Persistenz-API angenommen.
- Persistenztests für vollständigen Event-Round-trip, Prozessneustart,
  Einfügereihenfolge, Sequenz-Paginierung und Aufbewahrungsgrenzen ergänzt.
- Explizit versionierten HTTP/JSON-Vertrag unter `/api/v1` für Session-,
  Membership- und Transmission-Anwendungsfälle dokumentiert.
- Frameworkfreien `hvc-network`-Adapter mit stabilen Fehlercodes,
  Eingabevalidierung und einheitlichen JSON-Envelopes implementiert.
- Externe Bearer-Credentials auf die Session-Erstellung begrenzt; geschützte
  Endpunkte akzeptieren ausschließlich ausgestellte, gerätegebundene Sessions.
- Membership-Antworten auf den authentifizierten Spieler und
  Transmission-Antworten auf die Empfängeranzahl ohne interne Empfänger-IDs
  beschränkt.
- Linux-HTTP/1.1-Listener mit Größenlimits, Timeouts und genau einem Request pro
  Verbindung an den Control-Plane-Entry-Point angebunden.
- Dateibasierten Bootstrap-Authenticator, zufällige Session- und
  Transmission-IDs sowie persistente Session-Auflösung im Runtime-Store
  verdrahtet.
- Netzwerkadaptertests für Authentifizierungsgrenze, Gerätebindung,
  Membership-Datenschutz, Transmission-Lebenszyklus und fehlerhafte Eingaben
  ergänzt.
- Produktionsfähige Account-/Identity-Grenze mit getrennten Schnittstellen für
  externe Identitätsprüfung und interne Session-ID-Ausstellung eingeführt.
- Serverseitige Session-Laufzeit auf das Minimum aus Provider-Vorgabe und
  Control-Plane-Richtlinie begrenzt; Provider-Ablehnungen bleiben typisiert.
- Den dateibasierten Bootstrap-Login als austauschbaren Identity-Provider hinter
  die neue Grenze verschoben und die Session-Erstellung separat getestet.
- Separat autorisierte administrative Lese- und Löschendpunkte für Memberships
  ergänzt; normale Sessions erhalten keine impliziten Verwaltungsrechte.
- Kurzlebige Voice-Grant-Claims werden serverseitig aus gerätegebundener Session,
  aktueller Membership-Version und Rollenrichtlinie abgeleitet.
- Separat autorisierte administrative Compare-and-Replace-Updates für einzelne
  Spieler-Memberships über `PUT /api/v1/admin/memberships/{player-id}` ergänzt.
- Strikt aufsteigende Membership-Versionen und atomarer Transmissionsabbruch
  bleiben auch über den Netzwerkvertrag erhalten.
- Eigenständigen LiveKit-Tokenadapter mit HS256-Signatur, getrennten
  Scope-Räumen und unabhängigen Publish-/Subscribe-Rechten implementiert.
- Offizielles natives LiveKit-C++-SDK 1.4.0 für Windows x64 mit fest
  hinterlegtem SHA-256 reproduzierbar und opt-in an CMake angebunden.
- Nativen Quality-Gate-Client ergänzt, der sich per URL und serverseitig
  ausgestelltem Token verbindet und die Anwesenheit eines zweiten
  Windows-Clients prüft.
- Zwei gleichzeitig laufende native Windows-Clients erfolgreich gegen einen
  lokalen LiveKit-Server verbunden; beide Verbindungen und der gemeinsame
  Raum wurden durch den Quality-Gate-Client bestätigt.
- Quality-Gate-Client um Windows-Audiogeräteauflistung und -auswahl,
  Mikrofonaufnahme mit WebRTC-Audioverarbeitung, Opus-Publikation sowie
  verifiziertes Remote-Opus-Abonnement mit Plattform-Playout erweitert.
- Mikrofonaufnahme und direkte Opus-Publikation/-Subscription lokal mit zwei
  nativen Clientprozessen gegen LiveKit Server 1.13.4 nachgewiesen; Sender und
  Empfänger meldeten jeweils `PASS`.
- Parallelen Empfang der getrennten Team-, Specialization- und Group-Räume im
  nativen Quality-Gate-Client ergänzt. Ein lokaler Empfänger hielt alle drei
  Raumverbindungen gleichzeitig und bestätigte Remote-Opus-Mikrofontracks von
  drei parallelen Senderprozessen in sämtlichen Scopes mit `PASS`.
- Native PTT-Probe ergänzt: Der Sender veröffentlicht das Mikrofon nur für die
  vorgegebene PTT-Dauer, unpubliziert den Track beim Loslassen und hält die
  Raumverbindung aufrecht. Der Empfänger bestätigte Opus-Start und
  Track-Entfernung Ende-zu-Ende mit `PASS`.
- Nativen Reconnect nach einem harten LiveKit-Serverausfall und -neustart
  nachgewiesen. Die zuvor beendete PTT-Publikation blieb nach dem vollständigen
  Reconnect beendet; Raumzustand und leere Track-Publikationsliste wurden mit
  `PASS` bestätigt.

## Aktueller Stand

- Das technische Projektfundament, der transportunabhängige Domänenkern, der
  In-Memory-Transmissionslebenszyklus sowie die dauerhafte Session-,
  Membership- und Audit-Ablage sind vorhanden und lokal validiert.
- Der erste versionierte Control-Plane-Netzwerkadapter ist implementiert. Der
  plattformunabhängige Vertrag und Dispatcher sind unter Windows lokal
  validiert; der Linux-spezifische Socketpfad wurde durch Debian-CI erfolgreich
  gebaut und getestet.
- Der Anwendungskern ist für einen produktiven OIDC-/Account-Adapter
  vorbereitet. Die konkrete externe Provider-Anbindung ist eine
  Deployment-Entscheidung; der lokale Linux-Start verwendet weiterhin den
  Bootstrap-Provider.
- Die LiveKit-Tokenausstellung sowie die native Client- und Audioanbindung sind
  vorhanden.
- SQLite wird unter Windows über `winsqlite3` aus dem Windows SDK und unter
  Debian über das Systempaket `libsqlite3-dev` angebunden.
- Das LiveKit-C++-Quality-Gate wurde begonnen. SDK-Build, lokaler Programmstart
  und die Verbindung zweier nativer Clientprozesse sind reproduzierbar. Der
  Quality-Gate-Client kann Windows-Audiogeräte auflisten und auswählen,
  Mikrofon-Audio mit Echo Cancellation, Noise Suppression und Automatic Gain
  Control als Opus veröffentlichen sowie einen empfangenen `audio/opus`-Track
  über den nativen Plattform-Audiopfad wiedergeben. Zusätzlich kann er Team-,
  Specialization- und Group-Räume parallel verbinden und den gleichzeitigen
  Opus-Empfang in allen drei Scopes prüfen. PTT-Publikation und sauberer
  Track-Abbruch ohne Raum-Disconnect sind ebenfalls nachgewiesen. Der lokale
  Zwei-Prozess-, Drei-Scope-, PTT- und Serverneustart-/Reconnect-Nachweis ist
  bestanden. Der reale Zwei-Rechner-Nachweis mit Mikrofonen wird übersprungen.
- Die Spezifikation liegt unverändert im Projekt vor.

## Nächster Schritt

Audiogerätewechsel während einer laufenden nativen LiveKit-Sitzung nachweisen.

## Validierung

| Prüfung | Ergebnis |
|---|---|
| Windows MSVC Debug | Bestanden |
| Windows MSVC Release | Bestanden |
| CTest Debug | 9 von 9 Tests bestanden |
| CTest Release | 9 von 9 Tests bestanden |
| Lokale native LiveKit-Zwei-Client-Verbindung | Bestanden |
| Native Windows-Audiogeräteerkennung | Bestanden |
| Lokaler Mikrofon-/Opus-Zwei-Prozess-Nachweis | Bestanden |
| Mikrofon-/Opus-Nachweis auf zwei Windows-Rechnern | Übersprungen |
| Lokaler paralleler Drei-Scope-Opus-Empfang | Bestanden |
| Nativer PTT-Start und sauberer Track-Abbruch | Bestanden |
| Nativer LiveKit-Reconnect ohne automatische Transmission | Bestanden |
| Domänen-Zustandsautomat: Reconnect ohne automatische Transmission | Bestanden |
| clang-format | Bestanden |
| clang-tidy | Bestanden unter Debian 13 mit Clang 19 |
| CMake-Installation und Paketexport inklusive `hvc::domain` | Bestanden |
| Debian GCC Debug | Bestanden, 7 von 7 Tests |
| Debian GCC Release | Bestanden, 7 von 7 Tests |
| GitHub-CI-Gesamtlauf | Bestanden |

## Risiken

| Risiko | Status | Maßnahme |
|---|---|---|
| Reife und Integration des LiveKit-C++-SDK | Offen | Frühes Quality Gate und Transportabstraktion |
| Autoritative Isolation im Shared-Room-Modell | Offen | Zunächst getrennte Räume verwenden |
| Hintergrund-PTT für unterschiedliche HID-Geräte | Offen | Früher Hardware-Prototyp mit Raw Input |
| Windows 10 außerhalb des regulären Supports | Akzeptiert | Build 19045 unterstützen und Windows 11 mittesten |
| 200-Spieler-Skalierung | Offen | Früher Lasttest vor UI-Vollausbau |
