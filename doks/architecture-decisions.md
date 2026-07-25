# Architekturentscheidungen

**Stand:** 25. Juli 2026

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

## Server

- Eigene Serverkomponenten werden in C++20 entwickelt.
- Primäre Zielplattform ist Debian 13 x64.
- Ubuntu LTS kann als sekundäre Plattform unterstützt werden.
- Der initiale Betrieb erfolgt containerisiert mit Docker und Compose.
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

## Voice-Transport

- LiveKit wird als selbst betriebene WebRTC-SFU eingesetzt.
- Der Windows-Client verwendet das offizielle native LiveKit-C++-SDK.
- LiveKit wird ausschließlich über eine interne `IVoiceTransport`-Abstraktion
  angebunden.
- Die Fachlogik darf nicht direkt von LiveKit-Typen abhängen.
- Vor der vollständigen Integration muss ein technischer und sicherheitsbezogener
  Prototyp erfolgreich abgeschlossen werden.
- Mumble ist die bevorzugte Rückfalloption, falls das LiveKit-C++-Quality-Gate
  nicht bestanden wird.

## Routing und Sicherheit

- Mitgliedschaften, Rollen und Berechtigungen sind serverautoritativ.
- Der Client darf keine vertrauenswürdige Empfängerliste vorgeben.
- Zugriffsrechte auf Voice-Räume sind kurzlebig und werden serverseitig
  ausgestellt.
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
