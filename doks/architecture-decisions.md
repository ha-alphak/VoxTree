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
- HTTP, Persistenz, Tokenprüfung, Audit-Logging und Voice-Transport werden über
  Adapter angebunden.

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
