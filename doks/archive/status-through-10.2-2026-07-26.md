# Archivierter Projektstatus bis Abschnitt 10.2

**Berichtsdatum:** 26. Juli 2026
**Phase:** Last- und Sicherheitstests abgeschlossen – Auslieferungsreife als Nächstes<br>
**Gesamtstatus:** Gelb

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
- Aufnahme- und Wiedergabegerätewechsel während einer nativen LiveKit-Sitzung
  nachgewiesen. Der Aufnahmewechsel bleibt im verbundenen Raum; für aktives
  Playout verwendet die Probe wegen einer SDK-1.4-Einschränkung einen
  kontrollierten Raum-Reconnect. Opus-Publikation und -Abonnement wurden danach
  jeweils mit `PASS` bestätigt.
- Sofortigen serverseitigen Publish-Rechteentzug über
  `RoomService.UpdateParticipant` nachgewiesen. LiveKit entfernte den aktiven
  Opus-Track innerhalb des gesetzten Zwei-Sekunden-Limits; ein verbundener
  Empfänger bestätigte das Remote-Unpublish.
- Subscription-Isolation mit einem Token ohne `can_subscribe` nachgewiesen:
  Teilnehmer und aktiver Servertrack waren vorhanden, beim nicht berechtigten
  nativen Client entstand jedoch kein Opus-Abonnement.
- Cross-Room-Isolation mit zwei gleichzeitig verbundenen, raumgebundenen Tokens
  nachgewiesen. Teilnehmer und Track des fremden Raums blieben für den
  Empfänger unsichtbar.
- Wiederholbares Sicherheits-Quality-Gate-Skript ergänzt, das den lokalen
  LiveKit-Server bei Bedarf kontrolliert startet, kurzlebige Test-Tokens nur im
  Arbeitsspeicher hält und ausschließlich den selbst gestarteten Prozess wieder
  beendet.
- UI-unabhängige `hvc-client`-Bibliothek mit stabiler
  `IVoiceTransport`-Abstraktion angelegt.
- Vollständige Team-, Specialization- und Group-Grant-Prüfung sowie exklusiven
  PTT-Lebenszyklus im Client-Core umgesetzt.
- Nativen `LiveKitVoiceTransport`-Adapter mit gekapselten SDK-Typen,
  Drei-Raum-Verbindung, Remote-Audioereignissen, Opus-Mikrofonpublikation und
  Audiogeräteverwaltung ergänzt.
- Die im Quality Gate validierten AEC-, Noise-Suppression-, AGC-, DTX-,
  Unpublish- und Gerätewechselpfade in den produktiven Adapter übernommen.
- Aktive PTT-Publikationen werden vor Reconnect und Disconnect beendet und
  niemals automatisch wieder aufgenommen.
- Client-Core-Tests für Grant-Grenzen, PTT-Exklusivität und Reconnect-Abbruch
  ergänzt.
- HTTP-v1-Endpunkt für kurzlebige, geräte- und membershipgebundene Voice-Grants
  ergänzt und mit dem bestehenden LiveKit-Tokenadapter verbunden.
- UI-unabhängigen `ControlPlaneClient` für Session, Membership, Voice-Grants
  sowie Start und Ende von Transmissionen implementiert.
- Produktionsnahen Windows-HTTP-Transport auf Basis von WinHTTP mit
  HTTP-/HTTPS-Unterstützung, Zeitlimits und Größenbegrenzung ergänzt.
- `AuthorizedVoiceClient` verdrahtet serverseitige
  Transmission-Autorisierung und PTT so, dass Audio erst nach einer positiven,
  korrelierten Startantwort publiziert wird.
- Rollback der Servertransmission bei fehlgeschlagener Mikrofonpublikation und
  explizite Bereinigung nach einem Transportabbruch ergänzt.
- Clienttests für die vollständige Session-/Membership-/Grant-Kette,
  Autorisierung vor Publikation, Ablehnung ohne Audio und Rollback ergänzt.
- Separate Team-, Specialization- und Group-PTT-Aktionen mit mehreren
  alternativen Einzel- oder Kombinationsbindings implementiert.
- Geräteübergreifende und gerätespezifische Tastatur-/Mausbindings,
  Konfliktvalidierung, Autorepeat-Unterdrückung sowie Hot-Unplug-Freigabe im
  UI-unabhängigen Eingabekern ergänzt.
- Win32-Raw-Input-Adapter mit eigenem unsichtbarem, nicht aktivierbarem Fenster,
  `RIDEV_INPUTSINK`, Geräteidentität und `RIDEV_DEVNOTIFY` umgesetzt.
- Eingabeaktionen über eine testbare PTT-Zielgrenze mit dem
  autorisierten Voice-Client-Lebenszyklus verbunden.
- Eingabekern- und Windows-Lifecycle-Tests ergänzt.
- Generic-Desktop-Usages für Gamepads, Joysticks und Multi-Axis-/HOTAS-Geräte
  im fokusunabhängigen Raw-Input-Pfad registriert.
- HID-Preparsed-Data und Button-Capabilities über die Windows-HID-Parser-API
  ausgewertet; mehrere Report-IDs und Press-/Release-Differenzen unterstützt.
- Geräteprofile um Geräteklasse, VID/PID, Top-Level-Usage und verfügbare
  HID-Buttons vervollständigt sowie Startinventar und Hot-Plugging verdrahtet.
- Reproduzierbare `hvc-input-quality-gate`-Probe für Geräteinventar,
  physische Ereignisse und Fremdfokusprüfung ergänzt.
- Auf der Entwicklungsmaschine drei Joystick/HOTAS-Collections erkannt; zwei
  melden 32 beziehungsweise 72 Buttons.
- Reale HOTAS-/Joysticktaste bei fremdem Vordergrundfenster empfangen und über
  Windows' `RIM_INPUTSINK`-Kennzeichnung als Hintergrundereignis bestätigt.
- Erste Windows-App-SDK-/WinUI-3-Clientschale mit Server- und Anmeldeansicht
  angelegt.
- Control Plane, nativen LiveKit-Transport und Raw Input bis zum verbundenen
  Bereitschaftszustand in der Clientschale verdrahtet.
- Vorläufige globale Team-, Specialization- und Group-PTT-Bindings auf
  `F9`, `F10` und `F11` aktiviert und die aktuelle Membership in der
  Bereitschaftsansicht dargestellt.
- Windows App SDK `2.2.0` und C++/WinRT `3.0.260715.1` als reproduzierbare,
  opt-in NuGet-Abhängigkeiten in ein eigenes CMake-Preset aufgenommen.
- Voice-Routing-Stufe mit getrennten Team-, Specialization- und Group-Räumen,
  serverautoritativer PTT-Prüfung und atomarem Abbruch bei Disconnect, Timeout,
  Membership- oder Rechteänderung abgeschlossen.
- Unabhängige Sende- und Empfangsrechte bis in die Voice-Grant-Ableitung
  durchgängig umgesetzt; ein Empfangsverbot entzieht kein vorhandenes
  Senderecht.
- Explizite automatisierte Cross-Team- und Cross-Group-Raumisolation in den
  LiveKit-Token-Tests ergänzt und die Routingstufe in `voice-routing.md`
  dokumentiert.
- Transportneutrale Audio-Policy mit globaler und scopebezogener
  Stream-Admission sowie deterministischer Priorität Group, Specialization,
  Team implementiert.
- Konfigurierbares hierarchisches Ducking, lokales Mute/Block und individuelle
  lineare Teilnehmerlautstärke mit sofortiger Neuberechnung aktiver
  Remote-Spuren umgesetzt.
- LiveKit-Räume auf selektive Subscription umgestellt, sodass nicht zugelassene
  Spuren keinen lokalen Decoder belegen.
- Dekodierte LiveKit-Opus-Spuren über getrennte XAudio2 Source Voices mit
  unabhängigem Gain, begrenzten Puffern und eigenem Playout-Worker an den
  nativen Windows-Ausgang angebunden.
- Client-Core-Tests für Prioritätsverdrängung, Ducking-Beziehungen,
  Mute/Block, individuelle Lautstärke und deterministische Wiederzulassung
  ergänzt; Audio-Engine in `audio-engine.md` dokumentiert.
- WinUI-Client um vollständige Hierarchie- und Membership-Anzeige sowie
  getrennte Verbindungs-, Sende-, Empfangs- und Fehlerzustände erweitert.
- Strukturierte ClientSession-Ereignisse für Voice-Zustand, bestätigte lokale
  Transmissionen und aktive Remote-Sprecher bis in die Oberfläche geführt.
- Sprecheransicht mit Scope, öffentlicher Teilnehmeridentität, Rollenfeld,
  bestätigtem Sprechzustand, lokaler Lautstärke und lokalem Mute ergänzt.
- Audioeinstellungen für Aufnahme-/Wiedergabegerät, Streamlimits und alle drei
  hierarchischen Ducking-Beziehungen mit der bestehenden Audio-Engine
  verdrahtet.
- Eingabeeinstellungen für unabhängige Team-, Specialization- und
  Group-PTT-Funktionstasten sowie Anzeige der Raw-Input-Geräteprofile ergänzt.
- Textskalierung, verstärkte Sprecherindikatoren und durchgängige textuelle
  Scope-/Statuskennzeichnung als Accessibility-Grundlage umgesetzt.
- Rollenabhängigen Moderationsbereich ergänzt; er ist ausschließlich für
  Memberships mit Moderator- oder Administratorrolle sichtbar.
- Sämtliche statischen UI-Texte in deutsche und englische Windows-Ressourcen
  ausgelagert.
- Reproduzierbaren WinUI-`TextBox`-/`PasswordBox`-Startfehler der
  selbstenthaltenen Windows-App-SDK-2.2.0-Runtime durch einen eng gekapselten,
  lokalisierten und passwortmaskierten nativen Anmeldedialog umgangen.
- UI-unabhängigen Client-Core als `hvc-client-core` Shared Library mit opakem
  Handle, expliziten Exporten und versionierter C-ABI `1.0` bereitgestellt.
- Größenmarkierte, erweiterbare C-Strukturen und stabile Enum-/Fehlerwerte für
  Scope-Grants, Memberships, Verbindung, Sprecher und Transportfehler
  definiert.
- Hostseitige Voice-Transportfunktionstabelle mit synchroner
  Observer-Abmeldung, exklusiver Mikrofonpublikation sowie Stream-Admission und
  Gain-Steuerung an den bestehenden `VoiceClient` angebunden.
- Versionierte Membership-, Membership-Clear-, Verbindungs-, Sprecher- und
  Fehlerereignisse mit pro Handle strikt steigender Sequenz implementiert.
- Header-only C++20-RAII-Wrapper mit besitzenden Eventwerten und idiomatischen
  Transport-, Membership- und Grant-Typen ergänzt.
- C11-Header-Compile-Gate und DLL-Verbrauchertests für ABI-Versionierung,
  Lebensdauer, Membership-Versionen, Reconnect-Invariante, PTT,
  Speaker-Admission und Fehlerereignisse ergänzt.
- Kompilierbare C++-Beispielintegration sowie verbindliche Dokumentation zu
  ABI-Kompatibilität, Threading, Besitz, Deployment und der
  Control-Plane-Autorisierung vor Audiopublikation erstellt.
- DLL, Importbibliothek, C-/C++-Header und das CMake-Ziel
  `hvc::client_core` in Installation und Paketexport aufgenommen.

- Dateibasierten Mehrbenutzer-Identity-Adapter mit Geräte-Whitelist und
  zeitkonstantem Credentialvergleich an den Linux-Entry-Point angebunden.
- Membership-Administration und Moderationsabbrüche über die aktuelle
  autoritative `administrator`-/`moderator`-Rollenbelegung freigeschaltet.
- LiveKit-Grants auf initial `canPublish=false` umgestellt und
  RoomService-Control für atomaren Start sowie jeden terminalen Endpfad
  verdrahtet.
- Sprecherlimits pro konkretem Team-, Specialization- und Group-Knoten unter
  demselben Store-Lock wie die Aktivierung durchgesetzt.
- Scheduler für 250-ms-Transmission-Timeouts, Session-Bereinigung und
  30-tägige Audit-Aufbewahrung in den Serverprozess integriert.
- Membership-Polling im Windows-Client mit 500-ms-Intervall ergänzt; neue
  Versionen erneuern Grants, Raumverbindungen, Empfangsabonnements und UI ohne
  automatische Fortsetzung einer Transmission.
- Linux-HTTP-Listener durch begrenzten Worker-Pool mit Queue-Überlastschutz,
  Signal-Shutdown und Drain ersetzt.
- Readiness, Prometheus-Metriken und strukturierte Betriebsereignisse für HTTP,
  aktive Transmissionen, LiveKit und SQLite ergänzt.
- Reproduzierbare Debian-13-Docker- und Compose-Auslieferung mit LiveKit
  `1.13.4`, persistenten Daten, Compose-Secrets und Caddy-TLS angelegt.
- Server-Laufzeit, Konfiguration, Sicherheitslebenszyklus, Jobs und Betrieb in
  `server-runtime.md` dokumentiert.
- Linux-Headless-Lasttreiber mit deterministischer Fixture für 200
  Group-Spieler sowie zwei isolierte Sicherheitsidentitäten implementiert.
- Reproduzierbare Compose-Lasttopologie mit getrennt und gemeinsam belasteter
  Control Plane und LiveKit sowie fest gepinntem offiziellem
  LiveKit-Lastgenerator angelegt.
- 202 unterschiedliche Spieler, Sessions und Memberships parallel
  authentifiziert und abgefragt; Group-Transmissionen mit exakt 200
  Empfängern, unabhängige Scopes, gleichzeitige Sprecher, Sprecherlimits,
  Moderation, Timeout und Membership-Abbruch bestanden.
- 200/200 Group-, viermal 4/4 unabhängige Scope-, 400/400
  Gleichzeitige-Sprecher- und 40/40 NetEm-Audioabonnements ohne Medienfehler
  erreicht.
- Serverausfall und Wiederherstellung unter fortlaufender Last, 150 ms
  NetEm-Delay mit 2 % Paketverlust sowie CPU-, Speicher-, Fehler- und
  Latenzmessungen automatisiert.
- Manipulierte Control-Plane-Anfragen, Publikation ohne Berechtigung,
  Publish-Rechteentzug bei aktivem Track, unzulässige Abonnements und
  Cross-Room-Isolation gegen einen nativen Windows-Client bestanden.
- Lastmodell, Ausführung, harte Gates, Messdefinitionen und Ergebnisartefakte
  in `load-and-security-tests.md` dokumentiert.

## Aktueller Stand

- Das technische Projektfundament, der transportunabhängige Domänenkern, der
  In-Memory-Transmissionslebenszyklus sowie die dauerhafte Session-,
  Membership- und Audit-Ablage sind vorhanden und lokal validiert.
- Der erste versionierte Control-Plane-Netzwerkadapter ist implementiert. Der
  plattformunabhängige Vertrag und Dispatcher sind unter Windows validiert.
  Der begrenzte Linux-Worker-Pool, Queue-Überlastschutz und Signal-Shutdown
  wurden auf Debian 13 in Debug und Release gebaut und durch einen realen
  Socket-Smoke-Test sowie ein dauerhaftes CTest-Regressions-Gate geprüft.
- Der Linux-Entry-Point unterstützt einen dateibasierten
  Mehrbenutzer-Identity-Adapter mit Gerätebindung und autorisiert
  administrative sowie moderierende Operationen aus der aktuellen Membership.
  Der Ein-Spieler-Bootstrap bleibt ausschließlich als lokaler Entwicklungsweg
  erhalten.
- LiveKit-Tokens starten publish-gesperrt. Der Server koppelt
  `UpdateParticipant` atomar an Start und alle terminalen Endpfade; ein
  fehlgeschlagener Control-Aufruf rollt die Aktivierung zurück.
- Transmission-Timeout, Session-Bereinigung und Audit-Aufbewahrung laufen als
  begrenzte periodische Jobs im Serverprozess. Sprecherlimits werden atomar pro
  konkretem Voice-Scope-Knoten durchgesetzt.
- Der Windows-Client erkennt Membership-Versionen durch 500-ms-Polling,
  beendet eine lokale Publikation und erneuert Grants, Raumverbindungen und
  Empfangsabonnements ohne automatische Fortsetzung.
- Dockerfile und Compose-Topologie sind implementiert. Native Builds sowie der
  vollständige Container-/Compose-Lauf sind auf Debian 13 bestanden,
  einschließlich TLS, Healthchecks, nicht-root Serverprozess, Secret-Staging
  und persistenter Session nach Container-Neuerstellung.
- Der reproduzierbare Abschnitt-10.2-Lauf ist auf Debian 13 mit 200
  Group-Spielern und zwei isolierten Sicherheitsidentitäten bestanden. 1.435
  Control-Plane-Gate-Anfragen und 11.851 parallele Hintergrundanfragen liefen
  ohne unerwarteten Fehler und ohne falschen Empfänger; unter dieser Last lag
  die p95-Startautorisierung bei 181,1 ms und die Membership-Propagation bei
  217,0 ms. Die konservativ gemessene Audio-Startzeit lag bei 445 ms für 200
  Subscriber und bei 1.531 ms unter kombinierter Zwei-Sprecher-Last. Getrennte,
  parallele, kombinierte und netzbeeinträchtigte Medienlast erreichte sämtliche
  erwarteten Audioabonnements. Ausfall und Wiederherstellung sowie die
  ergänzenden nativen Windows-Sicherheitsproben bestanden.
- Der UI-unabhängige Windows-Client-Core, die Control-Plane-Clientanbindung, der
  WinHTTP-Adapter, das globale Tastatur-/Maus-Eingabesystem und der native
  LiveKit-Transportadapter sind vorhanden. Generische HID-Controller sind für
  Buttons und Geräteprofile verdrahtet. Der physische
  HOTAS-Fremdfokus-Ereignisnachweis ist bestanden. Eine erste WinUI-3-Schale
  meldet sich an der Control Plane an, verbindet die autorisierten Voice-Räume,
  startet Raw Input und zeigt anschließend Membership und Bereitschaftsstatus.
- Die Audio-Engine priorisiert verfügbare Group-, Specialization- und
  Team-Spuren vor dem Dekodieren, wendet lokale Empfangsrestriktionen und
  konfigurierbare Gains an und gibt zugelassene Remote-Spuren über getrennte
  native XAudio2-Voices wieder.
- Die WinUI-3-Anwendung zeigt Hierarchie, Membership, PTT-Scopes, aktive
  Sprecher und getrennte Voice-Zustände an. Audio-, Eingabe- und
  Accessibility-Einstellungen greifen direkt auf die vorhandenen
  Client-Core-, LiveKit- und Raw-Input-Grenzen zu. Statische Texte sind
  vollständig als deutsche und englische Ressourcen vorhanden.
- Die Integrationsschnittstelle steht als installierbare Shared Library mit
  C-ABI `1.0` und C++20-Wrapper zur Verfügung. Der Host kann einen konkreten
  Voice-Transport ohne interne C++- oder LiveKit-Typen anbinden und erhält
  versionierte Membership-, Verbindungs-, Sprecher- und Fehlerereignisse.
  Control-Plane-Autorisierung bleibt als explizite vorgelagerte
  Sicherheitsgrenze dokumentiert.
- SQLite wird unter Windows über `winsqlite3` aus dem Windows SDK und unter
  Debian über das Systempaket `libsqlite3-dev` angebunden.
- Das LiveKit-C++-Quality-Gate ist abgeschlossen. SDK-Build, lokaler Programmstart
  und die Verbindung zweier nativer Clientprozesse sind reproduzierbar. Der
  Quality-Gate-Client kann Windows-Audiogeräte auflisten und auswählen,
  Mikrofon-Audio mit Echo Cancellation, Noise Suppression und Automatic Gain
  Control als Opus veröffentlichen sowie einen empfangenen `audio/opus`-Track
  über den nativen Plattform-Audiopfad wiedergeben. Zusätzlich kann er Team-,
  Specialization- und Group-Räume parallel verbinden und den gleichzeitigen
  Opus-Empfang in allen drei Scopes prüfen. PTT-Publikation und sauberer
  Track-Abbruch ohne Raum-Disconnect sind ebenfalls nachgewiesen. Der lokale
  Zwei-Prozess-, Drei-Scope-, PTT- und Serverneustart-/Reconnect-Nachweis ist
  bestanden. Aufnahme- und Wiedergabegeräte können innerhalb der
  Anwendungssitzung gewechselt werden; der Wiedergabewechsel erfordert mit dem
  aktuellen SDK einen kontrollierten Raum-Reconnect. Serverseitiger
  Rechteentzug, verweigerte Track-Subscriptions und die Isolation getrennter
  Räume sind ebenfalls lokal Ende-zu-Ende bestanden. Der reale
  Zwei-Rechner-Nachweis mit Mikrofonen wird übersprungen. Damit ist das
  LiveKit-Quality-Gate abgeschlossen.
- Die Spezifikation liegt unverändert im Projekt vor.

## Nächster Schritt

Mit Abschnitt 10.3 die Auslieferungsreife herstellen: Barrierefreiheit und
Bedienbarkeit mit unterschiedlichen Eingabegeräten prüfen, Datenschutz-,
Abhängigkeits- und Bedrohungsmodell abschließen, signiertes MSIX und
versionierte Linux-Container erzeugen sowie Betriebs-, Konfigurations-,
Moderations- und Integrationsdokumentation finalisieren.

## Validierung

| Prüfung | Ergebnis |
|---|---|
| Windows MSVC Debug | Bestanden |
| Windows MSVC Release | Bestanden |
| CTest Debug | 14 von 14 Tests bestanden |
| CTest Release | 14 von 14 Tests bestanden |
| Linux GCC Debug auf Debian 13 | 15 von 15 Tests bestanden, einschließlich Sanitizer |
| Linux GCC Release auf Debian 13 | 15 von 15 Tests bestanden |
| Linux-Server-Laufzeit-Smoke | Bestanden: Readiness, Metriken, Mehrbenutzer-Authentifizierung, Gerätebindung, SQLite-Migration und Signal-Shutdown |
| Linux-Queue-Überlastpfad | Bestanden: vollständige `503 server_overloaded`-Antwort ohne TCP-Reset |
| Docker-/Compose-Lauf | Bestanden auf Debian 13: Images, Healthchecks, TLS-Proxy, nicht-root PID 1, `0400`-Secrets und persistente Session |
| Client-Core-DLL und exportierte C-Symbole | Bestanden |
| Client-Core C-ABI `1.0` und C11-Header-Compile-Gate | Bestanden |
| Client-Core C++20-Wrapper und Beispielintegration | Bestanden |
| Client-Core Membership-, Verbindungs-, Sprecher- und Fehlerereignisse | Bestanden |
| Client-Core Reconnect ohne automatische Transmission | Bestanden |
| WinUI-3-Client Debug-Build | Bestanden |
| WinUI-3-Client Release-Build | Bestanden |
| WinUI-3-Client Start-Smoke-Test | Bestanden |
| WinUI-Hierarchie-, Sprecher- und Statusansicht | Bestanden |
| WinUI-Audio-, Eingabe- und Accessibility-Einstellungen | Bestanden |
| Deutsche/englische UI-Ressourcenparität | Bestanden |
| Lokale native LiveKit-Zwei-Client-Verbindung | Bestanden |
| Native Windows-Audiogeräteerkennung | Bestanden |
| Lokaler Mikrofon-/Opus-Zwei-Prozess-Nachweis | Bestanden |
| Mikrofon-/Opus-Nachweis auf zwei Windows-Rechnern | Übersprungen |
| Lokaler paralleler Drei-Scope-Opus-Empfang | Bestanden |
| Nativer PTT-Start und sauberer Track-Abbruch | Bestanden |
| Nativer LiveKit-Reconnect ohne automatische Transmission | Bestanden |
| Nativer Aufnahmegerätewechsel bei aktiver Opus-Publikation | Bestanden |
| Nativer Wiedergabegerätewechsel mit kontrolliertem Raum-Reconnect | Bestanden |
| LiveKit-Publish-Rechteentzug im isolierten Quality Gate | Bestanden |
| Control-Plane-gesteuerter LiveKit-Publish-Lebenszyklus | Bestanden, Adapter- und Lifecycle-Tests |
| Mehrbenutzer-Identity und autorisierte Membership-Verwaltung | Bestanden |
| Automatischer Transmission-Timeout im Serverprozess | Bestanden |
| Serverseitige Sprecherlimits pro Scope | Bestanden |
| Membership-Propagation an verbundene Clients | Bestanden, 500-ms-Polling |
| Parallele, überlastgeschützte Linux-HTTP-Verarbeitung | Bestanden auf Debian 13, einschließlich CTest-Regression und realem Socket-Smoke-Test |
| Headless-Lasttreiber und 200-Spieler-Ende-zu-Ende-Test | Bestanden: 202 unterschiedliche Sessions/Memberships, 200/200 Group- und 400/400 Zwei-Sprecher-Abonnements |
| Kombinierte Control-Plane-/Medienlast und Ressourcenmessung | Bestanden |
| NetEm mit 150 ms Delay und 2 % Paketverlust | Bestanden: 40/40 Abonnements, keine verbleibenden Medienfehler |
| Serverausfall und Wiederherstellung unter Last | Bestanden |
| Autorisierungs- und Membership-Latenz unter Last | Bestanden: p95 181,1 ms beziehungsweise 217,0 ms Propagation |
| Konservative Audio-Startlatenz | Gemessen: 445 ms für 200 Subscriber, 1.531 ms bei zwei Sprechern |
| Publikation ohne LiveKit-Publish-Berechtigung | Bestanden |
| Verhinderung nicht autorisierter LiveKit-Track-Abonnements | Bestanden |
| LiveKit-Cross-Room-Isolation | Bestanden |
| Automatisierte Cross-Team- und Cross-Group-Routingisolation | Bestanden |
| Windows-Client-Core und Fake-Transport-Tests | Bestanden |
| Audio-Engine: Admission, Ducking, Mute/Block und Lautstärke | Bestanden |
| Nativer selektiver LiveKit-/XAudio2-Playout-Build | Bestanden |
| Control-Plane-Client und autorisierte PTT-Koordination | Bestanden |
| Windows-WinHTTP-Transport Debug/Release | Bestanden |
| PTT-Bindings und separate Team-/Specialization-/Group-Aktionen | Bestanden |
| Win32-Raw-Input-Registrierung und Lifecycle | Bestanden |
| Joystick-/HOTAS-Geräteinventar und HID-Button-Capabilities | Bestanden |
| HID-Geräteprofile und Hot-Plug-Bereinigung | Bestanden |
| Hintergrund-PTT mit realem HOTAS-Button bei Fremdfokus | Bestanden |
| Nativer LiveKit-Transportadapter Release-Build | Bestanden |
| Domänen-Zustandsautomat: Reconnect ohne automatische Transmission | Bestanden |
| clang-format | Bestanden |
| clang-tidy | Bestanden unter Debian 13 mit Clang 19 |
| Linux-Headless-Treiber aus dem Release-Installationsbaum | Bestanden |
| CMake-Installation und Paketexport inklusive `hvc::domain`, `hvc::client` und `hvc::client_core` | Bestanden |
| Debian GCC Debug | Bestanden, 15 von 15 Tests |
| Debian GCC Release | Bestanden, 15 von 15 Tests |
| GitHub-CI-Gesamtlauf | Bestanden |

## Risiken

| Risiko | Status | Maßnahme |
|---|---|---|
| Reife und Integration des LiveKit-C++-SDK | Beobachten | Quality Gate bestanden; bekannte Geräte- und Publication-Handle-Einschränkungen im Adapter kapseln |
| LiveKit-Publikation ohne aktive Control-Plane-Transmission | Abgeschlossen | Initiale Tokens bleiben publish-gesperrt; Lifecycle-Hook steuert den exakten Raum |
| Linux-Server nur als Ein-Spieler-Bootstrap und mit sequenziellem HTTP-Listener | Abgeschlossen | Mehrbenutzer-Adapter und begrenzter Worker-Pool sind Standard im Deployment |
| Laufzeitjobs und Membership-Propagation | Abgeschlossen | Scheduler und 500-ms-Client-Polling sind verdrahtet |
| Sprecherlimits | Abgeschlossen | Aktivierungen werden atomar pro Scope und Hierarchieknoten begrenzt |
| Autoritative Isolation im Shared-Room-Modell | Zurückgestellt | Getrennte, erfolgreich isolierte Räume verwenden |
| Hintergrund-PTT für unterschiedliche HID-Geräte | Abgeschlossen | Tastatur/Maus und generische HID-Buttons umgesetzt; realer HOTAS-Button bei Fremdfokus über `RIM_INPUTSINK` bestätigt |
| Windows 10 außerhalb des regulären Supports | Akzeptiert | Build 19045 unterstützen und Windows 11 mittesten |
| 200-Spieler-Skalierung | Abgeschlossen | Reproduzierbarer Headless-Lauf mit 200 Group-Spielern und ergänzenden nativen Sicherheitsproben bestanden |
