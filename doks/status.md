# Projektstatus

**Berichtsdatum:** 27. Juli 2026<br>
**Phase:** Konsolidierung vor der Auslieferungsreife<br>
**Gesamtstatus:** Gelb<br>
**Release-Status:** Blockiert

## Kurzfassung

Domänenkern, Control Plane, sicherer LiveKit-Transport, Windows-Eingabesystem,
Audio-Engine, Client-Core-DLL sowie die reproduzierbaren Last- und
Sicherheitstests bis einschließlich Abschnitt 10.2 sind vorhanden. Die
technische Grundlage ist belastbar, der Windows-Client ist jedoch noch ein
Funktionsprototyp und keine auslieferungsreife Produktoberfläche. Seine
Präsentations- und Fensterarchitektur ist mit UX-01 jedoch in getrennte,
testbare Zustandsmodelle sowie Haupt-, Einstellungs- und Diagnosefenster
überführt. KDE-00 hat die technische Plattformgrundlage für Debian 13/KDE
implementiert und qualifiziert: offizielles LiveKit-C++-SDK, direkter
PipeWire-Medienpfad, evdev-/udev-Eingabe, XDG-Global-Shortcuts und ein
Qt-Widgets-Prototyp. Die eigentliche Debian-Produktoberfläche bleibt offen.
IAM-01 hat außerdem den geschlossenen Account-Lebenszyklus, den lokalen
Identity-Adapter, Argon2id, Geräte-/Sessionwiderruf, Identity-Datei-Migration
sowie Bootstrap und Recovery verbindlich festgelegt; die Implementierung folgt
in IAM-02/IAM-03.

Vor Abschnitt 10.3 müssen die Clientoberfläche, das Identitäts- und
Verwaltungsmodell sowie die Clientdiagnose vervollständigt werden. Der zuvor
beobachtete LiveKit-Publikationsfehler bei sehr kurzen PTT-Impulsen ist mit
PRE-01 behoben und als plattformneutrale sowie native Regression gesichert.

Der ausführliche historische Fortschritt und die vollständige frühere
Validierung sind archiviert:

- [Projektstatus bis Abschnitt 10.2](archive/status-through-10.2-2026-07-26.md)
- [Umsetzungsplan bis Abschnitt 10.2](archive/implementation-plan-through-10.2-2026-07-26.md)
- [PRE-01: Kurze PTT-Impulse](archive/pre-01-short-ptt-2026-07-26.md)
- [UX-01: Präsentations- und Fensterarchitektur](archive/ux-01-presentation-and-window-architecture-2026-07-26.md)
- [IAM-01: Identitäts- und Account-Lebenszyklus](archive/iam-01-identity-and-account-lifecycle-2026-07-27.md)
- [Last- und Sicherheitstests](load-and-security-tests.md)

## Belastbarer Ausgangsstand

| Bereich | Stand |
|---|---|
| Domäne und Routing | Datengetriebene Hierarchie, Rollenrechte, atomare Membership-Versionen und Isolation implementiert |
| Control Plane | Mehrbenutzersitzungen, Memberships, Transmissionen, Moderation, Sprecherlimits, Audit, Jobs und Metriken implementiert |
| Identität und Accounts | IAM-01-Zielmodell für geschlossene Provisionierung, Argon2id, Einladungen, Geräte/Sessions, Migration, Bootstrap und Recovery verbindlich entschieden; persistenter Dienst noch nicht implementiert |
| Voice | Drei getrennte LiveKit-Scope-Räume, servergesteuerte Publish-Rechte, bestätigungsgebundene PTT-Publikation, Opus-Aufnahme sowie XAudio2- und PipeWire-Backends implementiert |
| Last und Sicherheit | Reproduzierbarer 200-Spieler-Lauf und native Sicherheitsproben bestanden |
| Eingabe | Windows Raw Input sowie Wayland-Portal-PTT und unprivilegierter Linux-evdev-/udev-Adapter implementiert |
| Client-Core | UI-unabhängige Bibliothek sowie versionierte C-ABI und C++-Schnittstelle implementiert |
| Präsentation | Plattformneutrale Zustände, Befehle, Validierung und stabile Fehlercodes für Verbindung, Kanalwahl, Teilnehmer, Einstellungen, Administration und Diagnose implementiert und getestet |
| Windows-App | Prozesseinstieg, App, Haupt-, Einstellungs- und Diagnosefenster getrennt; Anmeldung, Verbindung, PTT, aktive Sprecher und technische Einstellungen als WinUI-3-Prototyp verdrahtet |
| Debian/KDE-App | Technische Qt-Widgets-/Wayland-Schale und Linux-Plattformadapter qualifiziert; vollständige Produktfenster, HTTP-, Einstellungs-, Secret-Service- und Diagnoseintegration fehlen |
| Auslieferung | Docker-Grundlage vorhanden; signiertes MSIX, Debian-Clientpaket, Produktbetrieb und Release-Dokumentation noch offen |

## Festgestellte Lücken und Probleme

### DIR-01: Kanäle und Teilnehmer

Der Client kennt über `GET /api/v1/membership` nur die eigene Membership.
Transportereignisse für verbundene Teilnehmer werden im `VoiceClient` derzeit
nicht an die Oberfläche weitergereicht; sichtbar werden Teilnehmer erst bei
einer aktiven Audiospur. Rollen und Anzeigenamen entfernter Teilnehmer fehlen.

Für eine Kanaldarstellung wird ein datenschutzbegrenztes Gruppenverzeichnis
benötigt. Es liefert die sichtbare Hierarchie und Teilnehmerdaten; der Client
verknüpft sie mit Transportpräsenz und Sprechzustand. Ein Kanal bleibt eine
serverabgeleitete Scope-Ansicht und ist keine frei wählbare Sicherheitsgrenze.

### SET-01: Einstellungen

Audio-, Eingabe- und Barrierefreiheitsoptionen liegen im eigenständigen
Einstellungsfenster, werden aber noch nicht persistent gespeichert. HID-Buttons
können technisch verarbeitet, aber noch nicht komfortabel angelernt werden.

Benötigt werden validierte persistente Benutzereinstellungen, vollständiges
Binding-Lernen, Rücksetzen/Übernehmen und eine verständliche Fehleranzeige.

### KDE-01: Debian-Client für KDE Plasma

KDE-00 hat das offizielle LiveKit-C++-SDK 1.4.0 für Linux x64 reproduzierbar
eingebunden und die Windows-Audiokopplung in XAudio2- und PipeWire-Backends
getrennt. Der native Qt-Widgets-Prototyp startet unter Plasma/Wayland, zeigt
Audio- und Eingabefähigkeiten und verarbeitet globale Portal-PTT-Ereignisse bei
Fremdfokus. Ein unprivilegierter evdev-/udev-Adapter bildet Controllerbuttons
auf die gemeinsamen Eingabeereignisse ab.

Offen bleibt die vollständige Produktoberfläche mit Linux-HTTP-Adapter,
Directory/Presence, persistenten XDG-Einstellungen, Secret Service/KWallet,
Diagnose und Debian-Paketierung. AEC benötigt vor der Aktivierung einen
zeitlich ausgerichteten Reverse-Wiedergabestream. Physisches
Gamepad-/Joystick-/HOTAS-Hot-Plugging bleibt mangels verfügbarer Testhardware
ein Hardware-Release-Gate.

Details und Nachweise stehen in [debian-kde-client.md](debian-kde-client.md)
und [kde-00-quality-gate.md](kde-00-quality-gate.md).

### IAM-02/IAM-03: Persistente Accounts und Anmeldung

IAM-01 ist abgeschlossen und in
[identity-and-account-lifecycle.md](identity-and-account-lifecycle.md)
verbindlich dokumentiert. Der produktionsnahe Server liest Accounts weiterhin
aus einer statischen tabulatorgetrennten Identity-Datei und die Windows-App
erwartet noch ein opakes Credential. Persistenter Account-, Credential-,
Einladungs-, Geräte- und Sessiondienst, versionierte Lifecycle-Endpunkte sowie
Aktivierungs-, Passwort- und Selbstverwaltungsoberflächen fehlen.

Die Identity-Datei bleibt bis IAM-02 ein befristeter Entwicklungs- und
Migrationspfad. Ihre Klartext-Bearer werden nicht als Passwörter übernommen.

### ADM-01: Rechte und Moderation

Die Control Plane kann eine einzelne bekannte Membership lesen, ersetzen oder
löschen und autorisiert diese Operation serverseitig über die Rolle
`administrator`. Der Client bietet dafür keine API und keine Oberfläche.
Listenendpunkte, Rollenrichtlinienverwaltung, effektive Rechtevorschau,
Accountzuordnung und eine Liste aktiver moderierbarer Transmissionen fehlen.

Die sichtbare Rollenprüfung im Client darf nur die Darstellung steuern. Jede
Änderung und Moderationsaktion muss weiterhin serverseitig autorisiert,
versioniert und auditiert werden.

### DIA-01: Clientdiagnose

Die Oberfläche zeigt nur den letzten Laufzeitfehler. Es gibt kein
strukturiertes, rotierendes Clientprotokoll und keinen exportierbaren,
bereinigten Diagnosebericht. Das vorhandene Startup-Log deckt den
Voice-Lebenszyklus nicht ab und wurde beim Release-Test nicht erzeugt.

Benötigt werden komponentenbezogene Laufzeitereignisse, Korrelations-IDs,
Verbindungszustände pro Scope, Geräte- und Eingabestatus, Versionen sowie ein
redigierter Support-Bundle-Export. Credentials, Session-IDs, Grant-Tokens und
Voice-Inhalte dürfen darin nicht enthalten sein.

### REL-01: Auslieferungsreife

Barrierefreiheit und Bedienbarkeit sind noch nicht mit realen
Benutzerabläufen und verschiedenen Eingabegeräten abgenommen. Datenschutz-,
Abhängigkeits- und Bedrohungsmodell, signiertes MSIX, versionierte
Linux-Container sowie Betriebs-, Konfigurations-, Account-, Moderations-,
Diagnose- und Integrationsdokumentation sind nicht abgeschlossen.

## Verbindliche Produktannahmen für die nächste Stufe

- Die Hauptansicht zeigt die serverdefinierte Hierarchie
  Group → Specialization → Team und die dazu sichtbaren Teilnehmer.
- Teilnehmer werden primär ihrem Team zugeordnet; die Auswahl eines höheren
  Scope zeigt die daraus abgeleitete Teilnehmermenge.
- Anzeigename, Präsenz, Sprechzustand und öffentlich sichtbare Rolle werden
  getrennt modelliert.
- Einstellungen öffnen in einem eigenen Fenster und blockieren die
  Hauptkommunikation nicht.
- Windows und Debian/KDE verwenden dieselben UI-unabhängigen
  Präsentationsmodelle, Netzwerkverträge und Abnahmekriterien; nur
  Plattformadapter und Fensterschalen sind getrennt.
- Der Debian-Client zielt auf Debian 13 x64 mit KDE Plasma und Wayland.
  Sicherheitsgrenzen des Compositors werden nicht durch privilegiertes
  Tastatur- oder Mausevent-Abgreifen umgangen.
- Version 1 verwendet geschlossene, administrativ provisionierte Accounts;
  offene Selbstregistrierung bleibt außerhalb des Release-Umfangs.
- Accountstatus, Membership und Rollenrechte sind getrennte Konzepte.
- Administratoren verwalten Accounts, Hierarchie, Memberships und Rollen.
  Moderatoren verwalten aktive Transmissionen, aber keine Accounts.
- Serverautorisierung bleibt für jede Verwaltungsoperation verbindlich.
- Diagnoseartefakte sind standardmäßig datensparsam und vor dem Export
  automatisch redigiert.

## Nächster Schritt

Nicht mit Abschnitt 10.3 beginnen. Als nächstes API-01 des
[aktuellen Umsetzungsplans](implementation-plan.md) umsetzen: die versionierten
Directory-, Presence-, Account- und Verwaltungsverträge einschließlich
Autorisierungs-, Datenschutz-, Paging- und Konfliktregeln festlegen. Danach
folgen DIR-01 und die darauf aufbauenden Clientfunktionen.

## Release-Gates

Der Wechsel zu Abschnitt 10.3 ist erst zulässig, wenn:

- Kanalbaum und Teilnehmerzustände mit zwei realen Windows-Clients
  Ende-zu-Ende funktionieren;
- ein Debian/KDE-Client unter Wayland denselben sicheren Verbindungs-, PTT-,
  Empfangs-, Reconnect- und Diagnoseablauf bestanden hat;
- Windows und Debian/KDE in beide Richtungen miteinander sprechen können;
- Einstellungen ausschließlich im Einstellungsfenster bedient und persistent
  angewendet werden;
- Account-, Membership- und Rechte-Lebenszyklen über autorisierte Oberflächen
  vollständig verwaltbar und auditiert sind;
- der Diagnoseexport einen Fehler über Client, Control Plane und LiveKit
  korrelierbar macht, ohne Geheimnisse offenzulegen;
- die neuen Client-, Netzwerk- und Verwaltungspfade automatisiert getestet
  sind;
- die Bedienbarkeits- und Barrierefreiheitsabnahme bestanden ist.

## Aktive Risiken

| Risiko | Auswirkung | Behandlung |
|---|---|---|
| Fehlender Directory-/Presence-Vertrag | Kanalansicht kann keine vollständigen Teilnehmer zeigen | DIR-01 vor der Teilnehmer-UI implementieren |
| Statische Identity-Datei | Kein sicherer Account-Lebenszyklus im Produktbetrieb | IAM-01-Entscheidung in IAM-02/IAM-03 und API-01 umsetzen; Datei nur als Migrationspfad |
| Verwaltungs-API nur teilweise vorhanden | Rechteoberfläche wäre unvollständig oder unsicher | ADM-01 serverseitig vervollständigen, danach UI anbinden |
| Unzureichende Clientlogs | Feldprobleme sind nicht reproduzierbar | DIA-01 früh als Querschnittsfunktion einführen |
| AEC und physischer HID-Hot-Plug noch nicht auf Debian-Hardware qualifiziert | Echoqualität oder einzelne Controller können im Produktbetrieb abweichen | Reverse-Audiopfad implementieren; Hardwarematrix vor Release abnehmen |
| Globale PTT-Eingaben unter Wayland sind absichtlich eingeschränkt | Nicht alle Windows-Bindingtypen lassen sich identisch abbilden | XDG-Portal für Tastatur, separater HID-Adapter und explizite Capability-Anzeige |
| Unterschiedliche Audio-Backends | Abweichungen bei Geräten, AEC und Reconnect | Gemeinsame Audio-Verträge, PipeWire-Adapter und plattformübergreifende Abnahmematrix |
| Windows 10 außerhalb des regulären Supports | Höherer Test- und Supportaufwand | Build 19045 explizit testen und Supportentscheidung vor Release bestätigen |
