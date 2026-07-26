# Archivierter Umsetzungsplan bis Abschnitt 10.2

**Projekt:** Hierarchical Voice Communication  
**Stand:** 26. Juli 2026<br>
**Ziel:** Moderne, sichere und integrierbare Voice-Communication-Anwendung für
Windows 10 und neuer

## 1. Projektfundament

- CMake-Projekt mit Presets für Debug, Release und automatisierte Tests anlegen.
- Visual Studio 2022 und MSVC/v143 als Windows-Toolchain konfigurieren.
- Linux-Toolchain für Debian 13 einrichten.
- Abhängigkeiten mit festgelegten Versionen verwalten.
- Compilerwarnungen, statische Analyse und Codeformatierung aktivieren.
- Unit-Test-Framework und CI-Build für Windows und Linux einrichten.
- Dokumentations- und Konfigurationskonventionen festlegen.

## 2. Domänenkern

- Starke Typen für Spieler-, Gruppen-, Hierarchie- und Transmission-IDs
  definieren.
- Datengetriebene Hierarchie ohne fest codierte Namen implementieren.
- Rollenbasierte Sende- und unabhängige Empfangsrechte modellieren.
- Immutable und versionierte Membership-Snapshots verwenden.
- Zustandsautomaten für Verbindung, Reconnect und Transmission umsetzen.
- Deterministische Empfängerermittlung als transportunabhängige Bibliothek
  implementieren.
- Routing, Berechtigungen und Zustandsübergänge vollständig mit Unit- und
  generativen Tests absichern.

## 3. Control-Plane für Linux

- Account-, Session- und Geräteanmeldung entwickeln.
- Gruppen, Specializations, Teams und Rollen verwalten.
- Kurzlebige Voice-Zugriffsrechte ausstellen.
- Start und Ende von Übertragungen serverseitig autorisieren.
- Membership-Versionen und atomare Aktualisierungen implementieren.
- Rate Limits, Transmission-Timeouts und Moderationsaktionen umsetzen.
- Strukturierte Audit-Logs und Korrelations-IDs ergänzen.
- Persistenz und Schema-Migrationen einrichten.

## 4. LiveKit Quality Gate

Vor dem Ausbau des Clients wird ein echter technischer Prototyp erstellt.

Er muss nachweisen:

- Verbindung von mindestens zwei Windows-Rechnern mit echten Mikrofonen.
- Publikation und Empfang von Opus-Audio.
- Paralleler Empfang von Team-, Specialization- und Group-Scope.
- Funktionsfähiger PTT-Start und sauberer Abbruch.
- Reconnect ohne automatisches Fortsetzen einer Transmission.
- Gerätewechsel während einer Sitzung.
- Sofortiger serverseitiger Rechteentzug.
- Verhinderung nicht autorisierter Raum- und Track-Abonnements.
- Reproduzierbare Auslieferung der nativen SDK-Abhängigkeiten.

Bei Nichterfüllung wird der Transportadapter gegen Mumble oder eine andere
geeignete Lösung ausgetauscht.

## 5. Voice-Routing

- Separate LiveKit-Räume für Group, Specialization und Team verwalten.
- Pro Client ausschließlich autorisierte Raum-Grants ausstellen.
- PTT-Scope serverseitig gegen Rolle und Membership prüfen.
- Aktive Übertragungen bei Disconnect, Timeout, Rollen- oder
  Membership-Änderung beenden.
- Cross-Team- und Cross-Group-Isolation automatisiert testen.
- Shared-Room-Routing erst nach einem gesonderten Security- und Lasttest
  evaluieren.

## 6. Audio-Engine

- Audioaufnahme und -wiedergabe auf Windows integrieren.
- Auswahl und Wechsel von Ein- und Ausgabegeräten unterstützen.
- Opus, Jitter Buffer, Paketverlustbehandlung und Pegelanzeige anbinden.
- Echo-Unterdrückung, Noise Suppression und automatische Pegelanpassung
  evaluieren und konfigurieren.
- Lokales Mute, Block und individuelle Lautstärke implementieren.
- Konfigurierbares Ducking und Stream-Admission umsetzen.
- Audio-Threads von UI- und Netzwerk-Threads sauber trennen.

## 7. Eingabesystem

- Separate Aktionen für Team-, Specialization- und Group-PTT definieren.
- Mehrere Bindings und Tastenkombinationen je Aktion unterstützen.
- Tastatur und Maus über Raw Input anbinden.
- Gamepads, Joysticks und HOTAS als HID-Geräte erkennen.
- Geräteidentität, Button-Zuordnung und Hot-Plugging behandeln.
- Hintergrund-PTT testen, während andere Anwendungen den Fokus besitzen.
- Konflikte, ungültige Belegungen und getrennte Geräteprofile anzeigen.

## 8. WinUI-Client

- Server- und Anmeldeansicht erstellen.
- Hierarchie und aktuelle Membership darstellen.
- Gewählten Scope und PTT-Belegungen deutlich anzeigen.
- Sende-, Empfangs-, Fehler- und Verbindungsstatus visualisieren.
- Sprecherliste mit Scope, Name, Rolle und Sprechzustand implementieren.
- Audio-, Eingabe- und Accessibility-Einstellungen bereitstellen.
- Moderationsoberfläche rollenabhängig einblenden.
- Sämtliche sichtbaren Texte lokalisierbar halten.

## 9. Integrationsschnittstelle

- UI-unabhängigen `hvc-client-core` stabilisieren.
- Eine stabile C-ABI als DLL-Grenze entwerfen.
- Idiomatischen C++-Wrapper bereitstellen.
- Versionierte Events für Membership, Verbindung, Sprecher und Fehler
  definieren.
- Optional eine lokale IPC-Schnittstelle für getrennte Prozesse anbieten.
- Beispielintegration und API-Dokumentation erstellen.

## 10. Qualität und Auslieferung

### 10.1 Server-Laufzeit vervollständigen

- Einen mehrbenutzerfähigen Identity- und Membership-Adapter anbinden. Der
  Ein-Spieler-Bootstrap-Provider bleibt ausschließlich ein lokaler
  Entwicklungsmodus.
- Administrative Membership- und Moderationsoperationen im Linux-Entry-Point
  mit einer autoritativen Rollenrichtlinie verdrahten.
- LiveKit-Publikationsrechte an den serverseitigen Transmissionslebenszyklus
  koppeln. Ein verbundener Client darf erst nach erfolgreichem Start für genau
  den autorisierten Scope publizieren; Ende, Timeout, Disconnect, Moderation
  sowie Membership- oder Rechteänderungen müssen das Recht entziehen und einen
  aktiven Track entfernen.
- Konfigurierbare Sprecherlimits pro Group-, Specialization- und Team-Scope
  atomar bei der Transmissionsaktivierung durchsetzen.
- Scheduler für Transmission-Timeouts, Session-Bereinigung und
  Audit-Aufbewahrung in den Serverprozess integrieren.
- Membership- und Rechteänderungen innerhalb des Zielwerts an verbundene
  Clients propagieren und dort Grants sowie Empfangsabonnements aktualisieren.
- Den sequenziellen Linux-HTTP-Listener durch eine begrenzte parallele
  Verarbeitung mit Überlastschutz und geordnetem Herunterfahren ersetzen.
- Readiness, strukturierte Betriebsprotokolle und Metriken für HTTP,
  Autorisierung, aktive Transmissionen, LiveKit-Operationen, SQLite und
  verworfene Audit-Events bereitstellen.
- Reproduzierbare Docker- und Compose-Konfiguration für Control Plane,
  LiveKit, persistente Daten und TLS-terminierenden Reverse Proxy erstellen.

### 10.2 Reproduzierbare Last- und Sicherheitstests

- Einen Headless-Lasttreiber für mindestens 200 unterschiedliche Spieler,
  Sessions und Memberships erstellen. Der Nachweis erfordert keine 200
  physischen Windows-PCs.
- Control-Plane-Last und LiveKit-Medienlast getrennt sowie gemeinsam messen.
- Eine Group-Transmission mit bis zu 200 berechtigten Empfängern und mehrere
  gleichzeitige, voneinander unabhängige Scope-Transmissionen testen.
- Gleichzeitige Sprecher, Sprecherlimits, Paketverlust und hohe Netzwerklatenz
  testen.
- Manipulierte Clients, Publikation ohne Startautorisierung und unzulässige
  Abonnements testen.
- Reconnect, Serverausfall, Timeout, Moderation und Membership-Wechsel unter
  Last prüfen.
- Autorisierungslatenz, Audio-Startlatenz, Membership-Propagation,
  Fehlerraten, Ressourcenverbrauch und falsche Empfänger automatisiert
  auswerten.
- Echte Mikrofon-, Wiedergabe- und Eingabegeräte ergänzend mit wenigen
  Windows-Rechnern Ende-zu-Ende prüfen.

### 10.3 Auslieferungsreife

- Barrierefreiheit und Bedienbarkeit mit verschiedenen Eingabegeräten testen.
- Datenschutz-, Abhängigkeits- und Bedrohungsmodell prüfen.
- Signiertes MSIX-Paket und versionierte Linux-Container erzeugen.
- Betriebs-, Konfigurations-, Moderations- und Integrationsdokumentation
  fertigstellen.

## Release-blockierende Kriterien

- Kein unautorisierter Empfänger erhält Audio oder vertrauliche Metadaten.
- Kein Client kann ohne aktive serverseitige Transmission publizieren.
- Timeout, Disconnect, Moderation sowie Membership- oder Rechteänderungen
  entziehen aktive LiveKit-Publikationsrechte innerhalb des Zielwerts.
- Konfigurierte Sprecherlimits werden serverseitig atomar durchgesetzt.
- Alle Acceptance Criteria der Spezifikation sind erfüllt oder ausdrücklich
  dokumentiert zurückgestellt.
- Der vereinbarte 200-Spieler-Lasttest besteht.
- Membership-Änderungen werden innerhalb des Zielwerts wirksam.
- Eine unterbrochene Transmission wird niemals automatisch fortgesetzt.
- Client und Server können reproduzierbar aus einem sauberen Checkout gebaut
  werden.
