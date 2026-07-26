# Projektstatus

**Berichtsdatum:** 26. Juli 2026<br>
**Phase:** Konsolidierung vor der Auslieferungsreife<br>
**Gesamtstatus:** Gelb<br>
**Release-Status:** Blockiert

## Kurzfassung

Domänenkern, Control Plane, sicherer LiveKit-Transport, Windows-Eingabesystem,
Audio-Engine, Client-Core-DLL sowie die reproduzierbaren Last- und
Sicherheitstests bis einschließlich Abschnitt 10.2 sind vorhanden. Die
technische Grundlage ist belastbar, der Windows-Client ist jedoch noch ein
Funktionsprototyp und keine auslieferungsreife Produktoberfläche. Ein
Desktop-Client für Debian 13 mit KDE Plasma ist neu in den verbindlichen
Vor-Release-Umfang aufgenommen und noch nicht implementiert.

Vor Abschnitt 10.3 müssen die Clientoberfläche, das Identitäts- und
Verwaltungsmodell sowie die Clientdiagnose vervollständigt werden. Außerdem ist
der beim Zwei-Client-Test beobachtete LiveKit-Publikationsfehler bei sehr kurzen
PTT-Impulsen zu beseitigen.

Der ausführliche historische Fortschritt und die vollständige frühere
Validierung sind archiviert:

- [Projektstatus bis Abschnitt 10.2](archive/status-through-10.2-2026-07-26.md)
- [Umsetzungsplan bis Abschnitt 10.2](archive/implementation-plan-through-10.2-2026-07-26.md)
- [Last- und Sicherheitstests](load-and-security-tests.md)

## Belastbarer Ausgangsstand

| Bereich | Stand |
|---|---|
| Domäne und Routing | Datengetriebene Hierarchie, Rollenrechte, atomare Membership-Versionen und Isolation implementiert |
| Control Plane | Mehrbenutzersitzungen, Memberships, Transmissionen, Moderation, Sprecherlimits, Audit, Jobs und Metriken implementiert |
| Voice | Drei getrennte LiveKit-Scope-Räume, servergesteuerte Publish-Rechte, Opus-Aufnahme und XAudio2-Wiedergabe implementiert |
| Last und Sicherheit | Reproduzierbarer 200-Spieler-Lauf und native Sicherheitsproben bestanden |
| Eingabe | Globale Tastatur-, Maus- und generische HID-/HOTAS-Eingaben implementiert |
| Client-Core | UI-unabhängige Bibliothek sowie versionierte C-ABI und C++-Schnittstelle implementiert |
| Windows-App | Anmeldung, Verbindung, PTT, aktive Sprecher und technische Einstellungen als WinUI-3-Prototyp verdrahtet |
| Debian/KDE-App | Eingeplant; gemeinsamer Client-Core nutzbar, Linux-Transport-, Audio-, Eingabe- und UI-Adapter fehlen |
| Auslieferung | Docker-Grundlage vorhanden; signiertes MSIX, Debian-Clientpaket, Produktbetrieb und Release-Dokumentation noch offen |

## Festgestellte Lücken und Probleme

### PRE-01: Sehr kurze PTT-Impulse

Beim Windows-Zwei-Client-Test protokollierte LiveKit achtmal
`publish time out`. Betroffen waren sehr kurze PTT-Start-/Endfolgen, bei denen
das Publikationsrecht bereits wieder entzogen wurde, bevor die
Track-Publikation vollständig ausgehandelt war. Erfolgreiche normale
Opus-Publikationen zeigen, dass der grundsätzliche Medienpfad funktioniert.

Release-Kriterium: Schnelles Drücken und Loslassen muss deterministisch
abgebrochen werden, ohne ausstehende Publikation, Server-Timeout oder
zurückbleibendes Publish-Recht.

### UX-01: Hauptoberfläche

Die aktuelle App erzeugt nahezu die gesamte Oberfläche in
`apps/windows-client/src/main.cpp` und hängt Hierarchie, Status, Sprecher,
Einstellungen und Moderationshinweis in eine einzige Scrollansicht. Sie besitzt
keine belastbare Navigation und trennt die Bedienaufgaben nicht.

Benötigt werden eine neu strukturierte Hauptansicht, eigenständige Fenster für
Einstellungen und Diagnose sowie rollenabhängige Verwaltungsansichten.

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

Audio-, Eingabe- und Barrierefreiheitsoptionen sind in der Hauptansicht
eingebettet und werden nicht persistent gespeichert. HID-Buttons können
technisch verarbeitet, aber noch nicht komfortabel angelernt werden.

Benötigt werden ein separates Einstellungsfenster, validierte persistente
Benutzereinstellungen, Gerätewechsel, vollständiges Binding-Lernen,
Rücksetzen/Übernehmen und eine verständliche Fehleranzeige.

### KDE-01: Debian-Client für KDE Plasma

Der UI-unabhängige Client-Core baut bereits unter Debian, aber der ausführbare
Desktop-Client ist aktuell vollständig Windows-spezifisch. WinUI, WinHTTP, Raw
Input und XAudio2 besitzen noch keine Linux-Adapter; auch das native
LiveKit-C++-SDK ist im Projekt nur für Windows qualifiziert.

Geplant ist ein nativer C++20-/Qt-6-Client für Debian 13 und KDE Plasma mit
Wayland als primärem Sitzungsmodell. PipeWire übernimmt Aufnahme, Wiedergabe,
Gerätewechsel und Pegelinformationen. Globale Tastatur-PTT-Aktionen werden
über das XDG-Global-Shortcuts-Portal integriert; HID-/HOTAS-Geräte erhalten
einen eigenen Linux-Adapter. Vor dem UI-Ausbau muss ein Linux-Quality-Gate
LiveKit, PipeWire, Wayland-PTT, Gerätewechsel und Reconnect nachweisen.

Details und bekannte Plattformgrenzen stehen in
[debian-kde-client.md](debian-kde-client.md).

### IAM-01: Anmeldung und Accounts

Der produktionsnahe Server liest Accounts derzeit aus einer statischen
tabulatorgetrennten Identity-Datei. Die Windows-App erwartet ein opakes
Credential. Es gibt keine Registrierung, Aktivierung, Passwortänderung,
Account-Deaktivierung, Geräte-/Sitzungsverwaltung oder Verwaltungsoberfläche.

Für Version 1 ist eine geschlossene, administrativ gesteuerte Registrierung
vorgesehen: Ein Administrator legt Accounts oder Einladungen an; eine offene
Selbstregistrierung ist nicht vorgesehen. Der bestehende
`IIdentityProvider` bleibt die Adaptergrenze. Die konkrete lokale
Credential-Speicherung und ein optionaler externer Identity-Provider werden vor
der Implementierung in einer Architekturentscheidung festgeschrieben.

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

Nicht mit Abschnitt 10.3 beginnen. Zuerst Phase 0 des
[aktuellen Umsetzungsplans](implementation-plan.md) abschließen:

1. PRE-01 reproduzierbar machen, korrigieren und als Regressionstest sichern.
2. Ziel-Informationsarchitektur und Zustandsmodelle für Hauptansicht,
   Einstellungen, Administration und Diagnose festschreiben.
3. Das Debian-/KDE-Quality-Gate für LiveKit, PipeWire, Wayland und globale
   PTT-Eingaben ausführen.
4. Verzeichnis-, Presence-, Account- und Verwaltungsverträge spezifizieren,
   bevor die neue UI darauf aufgebaut wird.

## Release-Gates

Der Wechsel zu Abschnitt 10.3 ist erst zulässig, wenn:

- PRE-01 ohne verbleibende LiveKit-Publish-Timeouts bestanden ist;
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
| LiveKit-Publikationsrennen bei kurzem PTT | Medienfehler und irreführender Sendestatus | PRE-01 vor UI-Ausbau beheben und mit Stressprobe absichern |
| Fehlender Directory-/Presence-Vertrag | Kanalansicht kann keine vollständigen Teilnehmer zeigen | DIR-01 vor der Teilnehmer-UI implementieren |
| Statische Identity-Datei | Kein sicherer Account-Lebenszyklus im Produktbetrieb | IAM-01 mit versioniertem Vertrag und administrativer Provisionierung |
| Verwaltungs-API nur teilweise vorhanden | Rechteoberfläche wäre unvollständig oder unsicher | ADM-01 serverseitig vervollständigen, danach UI anbinden |
| Unzureichende Clientlogs | Feldprobleme sind nicht reproduzierbar | DIA-01 früh als Querschnittsfunktion einführen |
| Monolithische WinUI-Datei | Änderungen sind schwer testbar und regressionsanfällig | Präsentationsmodell und Fensterkomponenten vor Funktionsausbau trennen |
| LiveKit-C++-SDK unter Debian nicht qualifiziert | Linux-Medienpfad könnte Adapterarbeit oder Transportentscheidung erzwingen | KDE-00-Quality-Gate vor dem Bau der Oberfläche |
| Globale PTT-Eingaben unter Wayland sind absichtlich eingeschränkt | Nicht alle Windows-Bindingtypen lassen sich identisch abbilden | XDG-Portal für Tastatur, separater HID-Adapter und explizite Capability-Anzeige |
| Unterschiedliche Audio-Backends | Abweichungen bei Geräten, AEC und Reconnect | Gemeinsame Audio-Verträge, PipeWire-Adapter und plattformübergreifende Abnahmematrix |
| Windows 10 außerhalb des regulären Supports | Höherer Test- und Supportaufwand | Build 19045 explizit testen und Supportentscheidung vor Release bestätigen |
