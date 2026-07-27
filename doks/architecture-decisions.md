# Architekturentscheidungen

**Stand:** 27. Juli 2026

## Produkt

- Das Produkt wird eine eigenständige Voice-Communication-Anwendung.
- Es wird kein Simulations- oder reiner Demonstrationsclient entwickelt.
- Das Produkt ist als Voice-zentrierte Alternative zu Discord und TeamSpeak
  vorgesehen.
- Eine spätere Integration in Spiele und andere Anwendungen wird durch einen
  UI-unabhängigen Client-Core und eine versionierte SDK-Schnittstelle
  vorbereitet.
- Textchat, Video, Bildschirmfreigabe und Dateiübertragung gehören nicht zu
  Version 1.

## Windows-Client

- Programmiersprache: C++20.
- Entwicklungsumgebung: Visual Studio 2022 mit MSVC/v143.
- Oberfläche: WinUI 3 mit C++/WinRT.
- Mindestplattform: Windows 10 22H2, Build 19045, x64.
- Windows 11 wird mit derselben Binärdatei unterstützt und getestet.
- Auslieferung erfolgt als signiertes MSIX-Paket.
- Das Buildsystem verwendet CMake Presets.
- Abhängigkeiten werden reproduzierbar und mit festgelegten Versionen verwaltet.
- Der UI-unabhängige Client verwendet für den Control-Plane-Vertrag eine eigene
  typisierte Protokollschicht hinter `IClientHttpTransport`.
- Der erste Windows-HTTP-Adapter verwendet die Betriebssystembibliothek
  WinHTTP und unterstützt HTTP für geschützte lokale Entwicklung sowie HTTPS
  für produktive, TLS-terminierte Endpunkte.

## Debian/KDE-Client

- Der Debian-Desktop-Client gehört zum Vor-Release-Umfang.
- Zielplattform ist Debian 13 x64 mit KDE Plasma; Wayland ist der primäre
  Sitzungsweg.
- Die Oberfläche verwendet C++20, Qt 6 und Qt Widgets. KDE-00 bestätigt diese
  Fensterschale mit nativer Wayland- und XDG-Portal-Integration; Kirigami
  würde für den Desktopumfang zusätzliche Laufzeit- und Packaging-Komplexität
  schaffen, ohne die gemeinsamen Präsentationsmodelle zu verbessern.
- WinUI und Qt verwenden dieselben UI-unabhängigen Präsentationsmodelle,
  Netzwerkverträge und Fehlercodes.
- Aufnahme und Wiedergabe werden über ein direktes PipeWire-Backend an den
  gemeinsamen Voice-Transport angebunden. Noise Suppression und AGC laufen
  über WebRTC Audio Processing. AEC wird erst aktiviert, wenn ein
  zeitlich ausgerichteter Reverse-Wiedergabestream bereitsteht und auf realer
  Hardware qualifiziert ist.
- Globale Tastatur-PTT-Aktionen verwenden unter Wayland das
  XDG-Global-Shortcuts-Portal. Der Client umgeht Compositor-Sicherheitsgrenzen
  nicht durch privilegiertes Eingabeabgreifen.
- Gamepads, Joysticks und HOTAS werden über einen unprivilegierten,
  hot-plug-fähigen Linux-HID-Adapter integriert.
- Das erste Auslieferungsformat ist ein versioniertes Debian-Paket mit
  signierten Repository-/Release-Metadaten. Eine Flatpak-Auslieferung erfordert
  eine gesonderte Packaging-Entscheidung.
- Das technische Debian-Quality-Gate KDE-00 ist die verbindliche
  Plattformgrundlage. Physische HID-Hot-Plug- sowie spätere
  Windows↔Debian-Produktinteroperabilität bleiben eigenständige Release-Gates.

## Server

- Eigene Serverkomponenten werden in C++20 entwickelt.
- Primäre Zielplattform ist Debian 13 x64.
- Ubuntu LTS kann als sekundäre Plattform unterstützt werden.
- Der initiale Betrieb erfolgt containerisiert mit Docker und Compose.
- Der Linux-HTTP-Adapter verwendet einen konfigurierbaren festen Worker-Pool
  mit begrenzter Queue, Überlastantwort und geordnetem Signal-Shutdown.
- Readiness und Prometheus-Metriken bleiben im internen Betriebsnetz; TLS wird
  durch den vorgeschalteten Reverse Proxy terminiert.
- Kubernetes wird erst bei nachgewiesenem Skalierungsbedarf eingeführt.

## Control-Plane-Anwendungsschicht

- Die Control-Plane besitzt einen frameworkunabhängigen Anwendungskern.
- Authentifizierte Sessions binden Spieler, Gerät und Ablaufzeitpunkt
  serverseitig.
- Membership-Snapshot und Rollenrichtlinie werden als konsistenter,
  autoritativer Kontext geladen.
- Ein Startbefehl für eine Transmission enthält weder eine vertrauenswürdige
  Sender-ID noch eine Empfängerliste.
- Der Sender wird aus der Session abgeleitet; die Empfänger werden durch den
  Domänenkern aus dem autoritativen Snapshot berechnet.
- Start- und Endanforderungen werden getrennt pro authentifiziertem Spieler
  begrenzt.
- Transmission-Timeouts und autorisierte Moderationsabbrüche werden im
  Repository atomar auf aktive Transmissionen angewendet.
- Die Moderationsberechtigung wird über eine eigenständige
  Anwendungsschnittstelle bezogen.
- Start, Ende, Ablehnung und erzwungener Abbruch erzeugen typisierte,
  korrelierte Audit-Events über eine frameworkunabhängige Sink-Schnittstelle.
- Audit-Events enthalten nur die Empfängeranzahl, niemals die interne
  Empfängerliste.
- HTTP, Persistenz, Tokenprüfung, Audit-Logging und Voice-Transport werden über
  Adapter angebunden.

## Control-Plane-Persistenz

- Persistenz bleibt über Anwendungsschnittstellen vom Control-Plane-Kern
  getrennt.
- Die erste dauerhafte Ablage verwendet SQLite. Unter Windows wird die
  Betriebssystembibliothek `winsqlite3`, unter Debian `libsqlite3` verwendet.
- Jede Datenbank enthält eine Migrationshistorie und eine ganzzahlige
  Schemaversion. Migrationen werden strikt aufsteigend und jeweils in einer
  Transaktion ausgeführt.
- Eine Datenbank mit einer neueren als der vom Programm unterstützten
  Schemaversion wird beim Start abgelehnt; ein stilles Downgrade findet nicht
  statt.
- Sessions werden mit Session-, Spieler- und Geräte-ID sowie Ablaufzeitpunkt in
  Millisekunden seit der Unix-Epoche gespeichert.
- Autoritative Membership-Kontexte werden vollständig mit Hierarchie,
  Memberships, Rollenzuweisungen und getrennten Sende-/Empfangsrechten
  gespeichert. Ein Kontext wird nur durch eine strikt höhere Version ersetzt.
- Strukturierte Transmission-Audit-Events werden synchron mit einer
  monotonen Einfügesequenz gespeichert. Das Schema enthält nur die
  Empfängeranzahl und keine Empfänger-IDs.
- Audit-Events können sequenzbasiert in stabiler Einfügereihenfolge gelesen
  werden. Eine zeitbasierte, mengenbegrenzte Löschoperation bildet die
  Grundlage für spätere Aufbewahrungsjobs.
- Der `InMemoryControlPlaneStore` kann SQLite als autoritative
  Membership-Quelle verwenden. Persistentes Versionsupdate und Abbruch einer
  betroffenen aktiven Transmission sind dann gegenüber allen
  Store-Operationen ein atomarer kritischer Abschnitt.
- Aktive Transmissionen bleiben flüchtig und werden nach einem Neustart niemals
  wieder aufgenommen.

## Control-Plane-Netzwerkvertrag

- Der erste Netzwerkvertrag verwendet HTTP/1.1 mit JSON und ist über den
  Basis-Pfad `/api/v1` explizit versioniert.
- Externe Bearer-Credentials werden ausschließlich am Session-Endpunkt
  akzeptiert und dort an `ISessionAuthenticator` übergeben. Nachgelagerte
  Anwendungsfälle erhalten nur die ausgestellte, gerätegebundene Session-ID.
- Die produktive Identity-Grenze trennt externe Credential-, Account-,
  Geräte- und Rate-Limit-Prüfung von der internen Session-ID-Ausstellung.
  Session-Laufzeiten werden serverseitig durch eine lokale Obergrenze
  eingeschränkt.
- Geschützte Requests benötigen zusätzlich Geräte- und Korrelations-ID.
- Membership-Antworten enthalten ausschließlich die Membership des
  authentifizierten Spielers.
- Transmission-Antworten dürfen eine Empfängeranzahl, niemals jedoch interne
  Empfänger-IDs enthalten.
- Der erste Linux-Listener ist ein begrenzter HTTP/1.1-Adapter ohne eigene
  TLS-Terminierung. Er wird nur an Loopback oder hinter einem
  TLS-terminierenden Reverse Proxy betrieben.
- Der initiale Bootstrap-Authenticator liest sein Credential aus einer Datei.
  Er ist eine klar abgegrenzte Integrationsstufe und wird durch einen
  produktionsfähigen Account-/Identity-Adapter ersetzt.
- API-01 erweitert `/api/v1` additiv. Der bestehende opake
  `POST /api/v1/sessions`-Vertrag wird nicht als Passwortanmeldung
  umgedeutet; persistente Clients verwenden getrennte `/auth`-Ressourcen.
- Directory wird ausschließlich aus der aktuellen eigenen Group abgeleitet
  und besitzt keinen frei wählbaren Group-Parameter. Membership, öffentlicher
  Directory-Datensatz und zusammengeführte Transport-Presence bleiben
  getrennt versioniert.
- Änderbare Account-, Profil-, Hierarchie-, Membership- und Rollenressourcen
  verwenden starke ETags und `If-Match`. Listen verwenden an Akteur, Filter
  und Snapshot gebundene opake Cursor.
- Öffentliche Authentifizierungsfehler unterscheiden unbekannte, deaktivierte,
  abgelaufene oder widerrufene Accounts und Secrets nicht. Verwaltungs- und
  Moderationsrechte werden für jeden Request serverseitig aus dem aktuellen
  autoritativen Kontext neu abgeleitet.

## Identität und Account-Lebenszyklus

- Version 1 verwendet geschlossene, administrative Provisionierung. Eine
  offene Selbstregistrierung und ein anonymer Passwort-Reset gehören nicht zum
  Release-Umfang.
- Account, öffentliches Profil, Membership und Rollenzuweisung sind getrennte,
  eigenständig versionierte Datensätze. Account-Deaktivierung löscht weder
  Membership noch Auditgeschichte.
- Der lokale selbst betriebene Identity-Adapter verwendet SQLite hinter
  `IIdentityProvider`. Die Adaptergrenze bleibt für einen späteren externen
  Provider bestehen.
- Passwörter werden ausschließlich mit der High-Level-API von libsodium als
  parametrisierte Argon2id-1.3-Hashes gespeichert. Der Dienst kalibriert 250
  bis 500 Millisekunden Hashzeit und akzeptiert keine Konfiguration unter
  19 MiB Speicher und zwei Iterationen.
- Accountanlage, Aktivierung und administrativer Reset verwenden 256 Bit
  starke Einmal-Secrets. Persistiert werden nur deren Hashes; ein Reset
  widerruft Credential, Sessions, Geräteerneuerungen und ältere Einladungen
  atomar.
- Zugriffssessions bleiben kurzlebig. Längerfristige Anmeldung verwendet pro
  Gerät rotierende Erneuerungssecrets mit Hash-at-Rest und
  Wiederverwendungserkennung. Clients speichern sie nur im jeweiligen
  Betriebssystem-Secret-Store.
- Der erste Administrator entsteht über einen einmaligen Offline-Bootstrap
  ohne Netzwerklistener. Verlust aller Administratorzugänge wird ausschließlich
  durch eine auditierte Offline-Recovery für einen bereits als Administrator
  zugewiesenen Account behandelt.
- Die statische Identity-Datei wird nicht als Passwortquelle übernommen.
  Migration erzeugt neue Accounts und einmalige Aktivierungssecrets unter
  Erhalt von `PlayerId`, Membership, Rollen und Auditnachweisen.
- Bedrohungsmodell, Zustandsautomaten, Migration und Recovery sind verbindlich
  in
  [`identity-and-account-lifecycle.md`](identity-and-account-lifecycle.md)
  festgelegt. API-01 versioniert die daraus folgenden Netzwerkverträge; IAM-02
  implementiert sie.

## Voice-Transport

- LiveKit wird als selbst betriebene WebRTC-SFU eingesetzt.
- Die Desktop-Clients verwenden das offizielle native LiveKit-C++-SDK, sofern
  das Debian-Quality-Gate dessen Linux-Eignung bestätigt.
- LiveKit wird ausschließlich über eine interne `IVoiceTransport`-Abstraktion
  angebunden.
- Die Fachlogik darf nicht direkt von LiveKit-Typen abhängen.
- Der öffentliche Transportvertrag verwendet stabile HVC-Typen und kapselt
  SDK-Fehler als typisierte Ergebnisse und Observer-Ereignisse.
- Der Client verbindet Team, Specialization und Group parallel über getrennte
  Grants. Höchstens ein Scope darf gleichzeitig das Mikrofon publizieren.
- Der LiveKit-Adapter entfernt eine aktive Mikrofonpublikation bei jedem
  Reconnect oder Disconnect; eine automatische Wiederaufnahme ist unzulässig.
- Ein vom SDK abgelehnter aktiver Wiedergabegerätewechsel wird durch einen
  kontrollierten Reconnect ausschließlich der bereits autorisierten Räume
  umgesetzt. Eine PTT-Publikation wird dabei nicht wiederhergestellt.
- Vor der vollständigen Integration muss ein technischer und sicherheitsbezogener
  Prototyp erfolgreich abgeschlossen werden.
- Mumble ist die bevorzugte Rückfalloption, falls das LiveKit-C++-Quality-Gate
  nicht bestanden wird.

## Routing und Sicherheit

- Mitgliedschaften, Rollen und Berechtigungen sind serverautoritativ.
- Der Client darf keine vertrauenswürdige Empfängerliste vorgeben.
- Zugriffsrechte auf Voice-Räume sind kurzlebig und werden serverseitig
  ausgestellt.
- Raumtokens starten ohne Publikationsrecht. Dieses wird ausschließlich für
  eine atomar aktive Control-Plane-Transmission über LiveKit RoomService
  erteilt und bei jedem terminalen Ereignis wieder entzogen.
- Änderungen von Mitgliedschaft oder Berechtigungen beenden eine laufende
  Übertragung atomar.
- Die erste sichere Ausbaustufe verwendet getrennte Voice-Räume für Group,
  Specialization und Team.
- Ein gemeinsamer Group-Raum mit servergesteuerter Selective Subscription ist
  eine spätere Optimierung und erfordert vorher einen Security-Nachweis.

## Verbindungs- und Transmission-Zustände

- Die Domänenzustände sind unabhängig vom Voice-Transport modelliert.
- Eine Transportverbindung gilt erst nach einem autoritativen Membership-Refresh
  und dem Wiederherstellen der Empfangsabonnements als bereit.
- Jeder Verbindungsverlust verwirft die bekannte Membership-Version und beendet
  eine angefragte oder aktive Transmission unmittelbar.
- Ein Reconnect stellt den ausgewählten lokalen Scope wieder her, setzt eine
  zuvor aktive Transmission aber niemals automatisch fort.
- Asynchrone Transmission-Antworten werden über eine Client-Transmission-ID
  korreliert, damit verspätete Antworten keine neue Transmission aktivieren.
- Membership-Änderungen müssen eine höhere Version besitzen, beenden aktive
  Transmissionen und erfordern erneut angewendete Empfangsabonnements.
- Der Desktop-Client publiziert das Mikrofon erst nach einer erfolgreichen,
  korrelierten Startautorisierung der Control Plane.
- Schlägt die lokale Publikation danach fehl, beendet der Client die bereits
  autorisierte Servertransmission unmittelbar als Rollback.

## Push-to-Talk und Eingabegeräte

- Team, Specialization und Group besitzen jeweils eine separate
  Push-to-Talk-Aktion.
- Pro Aktion können mehrere alternative Bindings hinterlegt werden.
- Unterstützt werden Tastatur, Maus, Gamepad sowie generische Joysticks und
  HOTAS-Geräte.
- Hintergrund-PTT wird über Win32 Raw Input und generische HID-Eingaben
  realisiert.
- Die Eingabeverarbeitung darf keine Tastenereignisse simulieren, verändern oder
  vor dem aktiven Spiel verbergen.

## Sprecherlimits

Alle Grenzwerte sind konfigurierbar. Die initialen Standardwerte sind:

| Grenzwert | Standard |
|---|---:|
| Gleichzeitig dekodierte Streams pro Client | 8 |
| Gleichzeitig angezeigte Sprecher | 8 |
| Group-Sprecher | 2 |
| Specialization-Sprecher | 4 |
| Team-Sprecher | 5 |

Die Priorität lautet Group, Specialization, Team. Priorisierung beeinflusst
Ducking und Stream-Admission, niemals die Empfängerberechtigung.

Die serverseitige Aktivierung zählt Sprecher atomar pro konkretem
Hierarchieknoten. Das Limit ist damit auch bei parallelen Startanforderungen
verbindlich.

Der Client lässt innerhalb eines Scopes zuerst veröffentlichte Spuren zuerst
zu. Höhere Scopes verdrängen niedrigere deterministisch. Standard-Ducking-Gains
sind `0,50` für Team unter Specialization, `0,25` für Team unter Group und
`0,50` für Specialization unter Group. Teilnehmerlautstärke und Ducking werden
als lineare Faktoren zwischen `0,0` und `1,0` multipliziert.
