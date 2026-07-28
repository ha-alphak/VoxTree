# UX-02 – Kanal- und Teilnehmeransicht

**Abgeschlossen:** 28. Juli 2026  
**Umfang:** Vollständige serverdefinierte Kanalnavigation und
Teilnehmerdarstellung im Windows-Client  
**Status:** Abgenommen

## Ziel und Paketgrenze

UX-02 macht die mit DIR-02 bereitgestellten Directory-, Presence- und
Voice-Zustände als Produktoberfläche sichtbar. Die Windows-Hauptansicht zeigt
den vollständigen gruppenbegrenzten Baum Group → Specialization → Team sowie
alle Teilnehmer des gewählten Knotens. Nicht Bestandteil sind persistente
Einstellungen, die Debian/KDE-Schale, Accountverwaltung und strukturierte
Clientlogs; diese bleiben in ihren eigenen Arbeitspaketen.

## Implementierter Vertrag

- `DesktopModel` bildet aus dem serverdefinierten Directory einen
  deterministischen Präorder-Baum mit Elternbezug, Tiefe, Sortierung,
  Teilnehmerzahl und Markierung des eigenen Zweigs.
- Die Auswahl akzeptiert nur bekannte Knoten mit passendem Scope-Typ. Team
  zeigt seine unmittelbaren Teilnehmer, Specialization und Group die
  deterministisch abgeleitete Menge aller untergeordneten Teams.
- Die Teilnehmerliste bleibt unabhängig vom Sprechzustand sichtbar und trennt
  Anzeigename, öffentliche Rollen, Presence, Audioverfügbarkeit sowie
  Sprechzustand und Scope.
- Die eigene Position, das Empfangsrecht, serverseitiges Transmit-Mute und der
  bestätigte PTT-Scope sind getrennt dargestellt. Eine Rollenbezeichnung wird
  nicht als wirksames Recht interpretiert.
- Lokale Lautstärke, Mute und Block werden über `VoiceClient` angewendet.
  Lokale Regeln bleiben bei Directory-Refreshes erhalten, solange der
  Teilnehmer sichtbar bleibt.
- `DirectoryPhase` unterscheidet `unavailable`, `loading`, `ready`, `stale` und
  `unauthorized`. Transiente Refreshfehler behalten den letzten autorisierten
  Stand als veraltet; Rechteverlust oder dauerhaft fehlendes Directory löschen
  Kanal-, Auswahl- und Teilnehmerdaten.
- Die WinUI-Seitenleiste und Teilnehmerfläche sind scrollbar, dynamisch aus
  dem Modell aufgebaut und verwenden ausschließlich lokalisierte statische
  Oberflächentexte. Englische und deutsche Ressourcen besitzen dieselben IDs.
- Lautstärke besitzt neben dem Slider zugängliche Minus-/Plus-Aktionen; Mute
  und Block sind echte Buttons. Alle vier Aktionen tragen lokalisierte Namen,
  Tooltips und stabile Automation-IDs.
- Directory- und Presence-Polling melden ihre Fehlerart strukturiert an die
  Oberfläche. Lautstärkeänderungen bleiben gebündelt und alle
  Teilnehmeroperationen laufen außerhalb des UI-Threads.
- `WinHttpTransport` übernimmt `X-HVC-API-Version`, `ETag` und `Retry-After`
  aus echten WinHTTP-Antworten. Damit erreichen Directory-ETags und
  Presence-Backoff den bereits typisierten Clientvertrag auch in der
  Windows-Produktoberfläche.

## Automatisierte Nachweise

Die regulären MSVC-Debug- und -Release-Builds bestanden jeweils alle 17
CTest-Tests. Die erweiterten Präsentationstests decken insbesondere ab:

- einen absichtlich ungeordneten Baum mit mehreren Specializations und Teams;
- deterministische Präorder-, Breadcrumb- und Teilnehmerableitung;
- Team-, Specialization- und Group-Auswahl;
- Ablehnung unbekannter IDs und nicht passender Scope-Typen;
- Lade-, Ready-, Stale- und Unauthorized-Lebenszyklen;
- datensparsames Löschen nach Rechteverlust;
- Erhalt lokaler Lautstärke-, Mute- und Blockzustände nach Refresh.

Der neue Windows-Loopback-Test `client.win_http_transport` sendet eine reale
HTTP-Antwort durch WinHTTP und prüft die Weitergabe von Protokollversion, ETag
und Retry-After. Er schützt den im Zwei-Client-Lauf gefundenen
Directory-Integrationspfad vor Regression.

Zusätzlich bestanden:

- vollständiger opt-in Windows-Client-Build mit MSVC `/W4 /WX`;
- Ressourcenparität der englischen und deutschen Stringtables;
- `clang-format --dry-run --Werror` für alle C++-Quellen;
- gezielte `clang-tidy`-Analyse der geänderten plattformneutralen Quellen;
- Doxygen-Dokumentationsgate;
- versteckter Start-Smoke der gebauten WinUI-Anwendung;
- Docker-Build des aktuellen Control-Plane-Standes mit GCC und Warnungen als
  Fehler.

Der Docker-Build benötigt durch die bereits vorhandene Linux-evdev-/udev-
Plattformquelle `libudev-dev`. Das Build-Image installiert diese
Entwicklungsabhängigkeit nun explizit.

## Reale Zwei-Client-Abnahme

Der aktuelle Quellstand wurde auf dem dedizierten Testserver gebaut und mit dem
vorhandenen gruppenbegrenzten 200-Spieler-Fixture betrieben. Zwei separate
Windows-Clientprozesse meldeten sich mit unterschiedlichen Testidentitäten an.
Der Lauf prüfte:

- Darstellung aller 45 Directory-Knoten und der vollständigen
  200-Teilnehmer-Gruppe;
- mindestens zwei gleichzeitig online sichtbare Teilnehmer;
- Auswahl des Group-Knotens und abgeleitete nicht sprechende Teilnehmer;
- vorhandene Lautstärke-, Mute- und Blocksteuerelemente;
- getrennte Presence-, Audio- und Sprechtexte.

Die gespeicherten Abnahmebilder belegen die vollständige Group-Ansicht und die
sichtbaren lokalen Controls in beiden Prozessen. Die selbstenthaltene
Windows-App-SDK-2.2.0-Laufzeit meldet dynamisch erzeugte Teilnehmercontrols im
Raw-UIA-Baum derzeit als `Custom`; stabile Automation-IDs, Namen und Tooltips
sind im XAML-Zustand gesetzt, die plattformweite Screenreader-Abnahme bleibt
wie geplant Bestandteil von QA-01.

Die Testcredentials und ausgestellten Voice-Grants wurden weder in
Abnahmeartefakte noch in die Dokumentation übernommen.

## Abnahme

| Kriterium | Nachweis | Ergebnis |
|---|---|---|
| Navigierbarer serverdefinierter Baum | Modellregression und reale WinUI-Ansicht | Bestanden |
| Höhere Knoten leiten Teilnehmer aus untergeordneten Teams ab | `presentation.desktop_model` und 200-Spieler-Lauf | Bestanden |
| Eigene Position sowie Empfang, Transmit-Mute und PTT getrennt | Modell- und WinUI-Rendering | Bestanden |
| Nicht sprechende Teilnehmer mit getrennten Facetten sichtbar | Modellregression und Zwei-Client-Lauf | Bestanden |
| Lokale Lautstärke, Mute und Block verdrahtet | `VoiceClient`-Anbindung und WinUI-Steuerelemente | Bestanden |
| Leer-, Lade-, Trenn-, Stale- und Unauthorized-Zustände explizit | Zustandsmodell, Ressourcen und Regressionstests | Bestanden |

## Offene Folgearbeit

KDE-01 muss dasselbe Präsentationsmodell in der vollständigen Qt-6-/KDE-Schale
verwenden und die gemischte Windows-/Debian-Abnahme ergänzen. Persistenz und
Binding-Lernen bleiben SET-01; strukturierte Laufzeitdiagnose und Support-Bundle
bleiben DIA-01/DIA-02.
