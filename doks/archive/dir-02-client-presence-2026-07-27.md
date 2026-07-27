# DIR-02 – Client-Präsenz

**Abgeschlossen:** 27. Juli 2026  
**Umfang:** Clientseitige Directory-/Presence-Verarbeitung und Zusammenführung
strukturierter Voice-Ereignisse  
**Status:** Abgenommen

## Ziel und Paketgrenze

DIR-02 führt die in DIR-01 bereitgestellten gruppenbegrenzten Directory- und
Presence-Daten mit den Remote-Ereignissen aus Team-, Specialization- und
Group-Raum im gemeinsamen Präsentationsmodell zusammen. Das Paket stellt die
vollständigen UI-unabhängigen Zustände bereit. Der sichtbare Kanalbaum und die
Darstellung nicht sprechender Teilnehmer gehören zu UX-02.

## Implementierter Vertrag

- `ControlPlaneClient` liest Directory typisiert, unterstützt
  `If-None-Match`/`ETag` und validiert die vollständige Hierarchie,
  Teilnehmerzuordnung und den Katalog öffentlich sichtbarer Rollen.
- Presence wird als versionierter Snapshot oder Delta verarbeitet. Der Client
  unterstützt `after_version`, `Retry-After` und den
  `presence_snapshot_required`-Fallback.
- `VoiceClient` meldet Verbindung, Participant Connect/Disconnect,
  Audio Available/Unavailable und Speaker Started/Stopped als strukturierte
  Ereignisse mit Transportgeneration und monotoner Ereignisfolge.
- Der native LiveKit-Adapter verwirft Callbacks abgelöster Raumgenerationen.
- `DesktopModel` führt dieselbe Spieler-ID über alle drei Scopes zusammen und
  hält Presence, Audioverfügbarkeit und Sprechen als getrennte Facetten.
- Pro Spieler, Scope und Facette verhindern eigene Sequenzstände, dass
  verspätete Ereignisse neue Zustände überschreiben oder unabhängig
  eintreffende Ereignisarten verwerfen.
- Reconnect und Membership-/Directory-Wechsel löschen die jeweils ungültigen
  lokalen Zustände und verlangen bei einer neuen Directory-Version eine neue
  Presence-Snapshot-Basis.
- `ClientSession` lädt Directory und Presence beim Verbindungsaufbau, pollt
  beide anschließend und reicht Netzwerk- sowie Voice-Ereignisse über den
  WinUI-Dispatcher an das gemeinsame Modell weiter.
- Die bestehende C-ABI bleibt kompatibel: Nur die bereits bekannten
  Speaker-Start/-Stop-Ereignisse werden an ABI-Verbraucher abgebildet.

## Automatisierte Nachweise

Die regulären Debug- und Release-Builds bestanden jeweils alle 16 CTest-Tests.
Die erweiterten Tests decken insbesondere ab:

- gültige Directory-/Presence-Antworten, ETag/304 und Snapshot-Pflicht;
- unvollständige Hierarchien, unbekannte öffentliche Rollen, doppelte Spieler
  und ungültige Presence-Modi;
- strukturierte Voice-Ereignisarten, monotone Sequenzen und
  Reconnect-Generationen;
- Mehrfachanwesenheit eines Spielers in allen drei Scopes;
- Snapshot-/Delta-Verarbeitung, veraltete Versionen und verspätete
  Transportereignisse;
- deterministisches Zurücksetzen lokaler Zustände nach Reconnect.

Zusätzlich bestanden:

- MSVC-Debug- und -Release-Build mit `/W4` und Warnungen als Fehler;
- vollständiger opt-in Windows-Client-Build einschließlich LiveKit-Adapter;
- `clang-format --dry-run --Werror`;
- gezielte `clang-tidy`-Analyse der geänderten plattformneutralen
  Produktionsquellen ohne Projektbefund;
- Doxygen-Dokumentationsgate für die geänderten öffentlichen C++-Verträge;
- natives LiveKit-Sicherheits-Quality-Gate mit 100 kurzen PTT-Zyklen je Scope,
  sofortigem Rechteentzug, Unauthorized-Publish-, No-Subscribe- und
  Cross-Room-Isolationsprobe.

## Abnahme

| Kriterium | Nachweis | Ergebnis |
|---|---|---|
| Strukturierte Connect-/Disconnect-Ereignisse bis ins Präsentationsmodell | VoiceClient-, ClientSession- und Präsentationstests | Bestanden |
| Mehrfachanwesenheit über drei Scopes korrekt aggregiert | `presentation.desktop_model` | Bestanden |
| Presence, Audio und Sprechen getrennt | Modellvertrag und Regressionstests | Bestanden |
| Reconnect, Membershipwechsel und veraltete Ereignisse deterministisch | Generationen, Facettensequenzen und Tests | Bestanden |
| DIR-01-Snapshot/-Delta-Vertrag im Client konsumiert | Control-Plane- und Präsentationstests | Bestanden |
| Native Transportgrenze weiterhin sicher | vollständiges LiveKit-Quality-Gate | Bestanden |

## Offene Folgearbeit

DIR-02 rendert bewusst keine neue Produktoberfläche. UX-02 muss den
serverdefinierten Baum und alle sichtbaren Teilnehmer auf Basis der nun
vorhandenen Zustände darstellen. Der reale Zwei-Client-Test der vollständigen
Kanal- und Teilnehmeransicht bleibt deshalb ein Phase-1-Release-Gate und ist
kein unerledigter Bestandteil von DIR-02.
