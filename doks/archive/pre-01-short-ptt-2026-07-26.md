# PRE-01 – Kurze PTT-Impulse

**Abschlussdatum:** 26. Juli 2026  
**Status:** Abgeschlossen

## Ausgangsfehler

Beim realen Windows-Zwei-Client-Test waren acht LiveKit-Meldungen
`publish time out` aufgetreten. Ein sehr kurzer Press-/Release-Impuls entzog
das serverseitige Publish-Recht, während das SDK die lokale Track-Publikation
noch aushandelte. Der Client behandelte bereits die Rückkehr von
`publishTrack()` als aktive Übertragung, obwohl noch keine belastbare
Publication-SID vorlag.

## Umsetzung

- `VoiceClient`, `AuthorizedVoiceClient` und `LiveKitVoiceTransport` führen die
  Publikationsphasen `idle`, `starting`, `active` und `stopping`.
- Jede angenommene PTT-Anfrage erhält eine strikt steigende Generation.
- Erfolg wird erst nach einer nicht leeren SID und dem passenden Eintrag in der
  lokalen LiveKit-Publication-Map gemeldet.
- Release, Disconnect und Membership-/Rechteänderung können `starting`
  nebenläufig abbrechen. Ein spätes Ergebnis derselben Generation wird
  unpubliziert und kann keinen neueren Start aktivieren.
- Der Starter besitzt die Control-Plane-Bereinigung. Dadurch wird die
  serverseitige Transmission bei konkurrierenden Abbruchpfaden genau einmal
  beendet.
- Sehr kurze Impulse halten die Publication nur bis zum sicheren Abschluss der
  SDK-internen Ereignisverarbeitung; normal gehaltene PTT-Starts erhalten
  dadurch keine zusätzliche Wartezeit.
- Fehlertexte enthalten Scope, Zustandsübergang, Generation und
  `ClientTransmissionId` als Korrelations-ID.
- Ein fehlendes XAudio2-Wiedergabegerät blockiert einen reinen Sender nicht
  mehr; Remote-Admission meldet weiterhin
  `audio_device_unavailable`.

## Regressionen

Die plattformneutralen Tests decken ab:

- 100 Abbrüche während `starting` je Team-, Specialization- und Group-Scope;
- genau einen serverseitigen Endaufruf bei einem konkurrierenden Release;
- Disconnect während `starting`;
- Membership-/Rechtewechsel während einer aktiven Publikation;
- fehlgeschlagene Mikrofon-/Geräteinitialisierung mit Server-Rollback;
- Reconnect ohne automatische Wiederaufnahme.

Das native Gate `Invoke-LiveKitSecurityQualityGate.ps1` besitzt zusätzlich
`-PttCyclesPerScope` und `-PttStressOnly`. Es verwendet den produktiven
LiveKit-Adapter, wertet Publikations-Timeouts und eine fehlgeschlagene
Publisher-Negotiation nach dem Abbruch als Fehler und prüft über RoomService,
dass in keinem Scope-Raum ein Track verbleibt. Das SDK 1.4.0 kann nach dem
erfolgreichen Unpublish noch eine intern verworfene
`local_track_published for unknown sid`-Ereigniskopie melden; sie ist nur bei
gleichzeitig bestandenem Adapter- und RoomService-Leerstandsnachweis zulässig.

## Abnahmenachweise

| Gate | Ergebnis |
|---|---|
| Windows Debug-Build und 14 CTest-Tests | Bestanden |
| Windows Release-Build und 14 CTest-Tests | Bestanden |
| Deterministische 100 Schnellzyklen je Scope | Bestanden |
| Nativer LiveKit-Release-Build | Bestanden |
| Nativer LiveKit-Stress, 100 Zyklen je Scope | Bestanden; maximal 622 ms (Team), 713 ms (Specialization), 720 ms (Group); kein Adapter- oder Server-Track verblieben |
| Bestehende LiveKit-Sicherheitsproben | Bestanden; Rechteentzug, Publikationsverbot, Subscription- und Cross-Room-Isolation sowie Mikrofon-/Opus-Publikation |
| clang-format | Bestanden; sämtliche C/C++-Quellen im Repository |
| clang-tidy | Bestanden; geänderte Implementierungen vollständig, geänderte Test- und Quality-Gate-Zeilen |
| Doxygen-Dokumentationsgate | Bestanden |
| WinUI-Client-Build und Fünf-Sekunden-Startprobe | Bestanden |

## Verbleibende Einschränkung

LiveKit C++ SDK 1.4.0 liefert kein eigenes abbrechbares
`publishTrack()`-Handle. Der Adapter korreliert deshalb über Generation,
Trackidentität und Publication-SID und wartet bei ausschließlich sehr kurzen
Impulsen bis zum sicheren Unpublish-Zeitpunkt. Diese Kapselung bleibt auf den
LiveKit-Adapter begrenzt.
