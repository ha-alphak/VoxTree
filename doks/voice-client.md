# Windows-Client-Core und Voice-Transport

**Stand:** 26. Juli 2026

## Schichtengrenze

Der UI-unabhängige Client-Core liegt in `libs/client`. Er kennt weder
LiveKit-Header noch native SDK-Typen. Die einzige Transportgrenze ist
`hvc::client::IVoiceTransport`.

Der Vertrag umfasst:

- Verbindung zu den autorisierten Team-, Specialization- und Group-Räumen,
- genau eine aktive Mikrofonpublikation für den gewählten PTT-Scope,
- Aufzählung und Auswahl stabiler Aufnahme- und Wiedergabegeräte-IDs,
- Transport-, Teilnehmer- und Remote-Audio-Ereignisse,
- Momentaufnahmen für Teilnehmerzahl und aktiven Remote-Audioempfang.

`VoiceClient` akzeptiert zwischen einem und drei nicht leere, eindeutige
Scope-Grants. Ein nicht berechtigter Scope wird nicht künstlich ergänzt. Der
Client verhindert parallele PTT-Übertragungen und verwirft seinen aktiven
PTT-Zustand bei jedem Reconnect oder Disconnect. Eine beendete Übertragung wird
nach einem Reconnect niemals automatisch fortgesetzt.

Die lokale Mikrofonpublikation wird getrennt als `idle`, `starting`, `active`
und `stopping` geführt. Jede angenommene PTT-Anfrage erhält eine strikt
steigende Generation. Ein Release während `starting` ist ein Abbruchwunsch;
ein verspätetes Ergebnis derselben Generation kann keine neuere Publikation
aktivieren.

Die Grant-Tokens werden ausschließlich an den Transportadapter weitergereicht.
Der Client-Core wertet sie nicht aus und protokolliert sie nicht.

## LiveKit-Adapter

Der Windows-spezifische Adapter
`hvc::livekit::LiveKitVoiceTransport` wird nur im opt-in Build mit dem
festgelegten nativen LiveKit-SDK erzeugt. Das Pimpl trennt sämtliche
LiveKit-Typen vom öffentlichen Header.

Aus dem bestandenen Quality Gate wurden folgende Pfade übernommen:

- bis zu drei gleichzeitig verbundene, getrennte Scope-Räume,
- native Mikrofonaufnahme mit Echo Cancellation, Noise Suppression und
  Automatic Gain Control,
- Opus-Publikation mit maximal 64 kbit/s, DTX und ohne RED,
- Unpublish beim Loslassen von PTT, ohne den Raum zu verlassen,
- sofortiges Entfernen einer aktiven Mikrofonpublikation vor einem
  Reconnect-/Disconnect-Ereignis,
- Remote-Opus-Erkennung und Plattform-Playout,
- Aufnahmegerätewechsel innerhalb der Sitzung,
- kontrollierter Reconnect aller autorisierten Räume, falls das SDK einen
  aktiven Wiedergabegerätewechsel ablehnt.

Der kontrollierte Wiedergabe-Reconnect verwendet nur die bereits
autorisierten Grants und startet keine zuvor aktive Mikrofonpublikation neu.

`LiveKitVoiceTransport::startMicrophone()` meldet Erfolg erst, wenn eine
nicht leere Publication-SID auch in der lokalen Publication-Map des Raums
sichtbar ist. Für einen ausschließlich sehr kurzen Impuls wartet der
Unpublish-Pfad bis zum sicheren Abschluss der SDK-internen
Publication-Ereignisverarbeitung. Späte Bestätigungen abgebrochener
Generationen werden unmittelbar unpubliziert.

Ein fehlendes XAudio2-Wiedergabegerät verhindert die Senderinitialisierung
nicht mehr. Aufnahme und Publikation bleiben verfügbar; erst die Admission
eines Remote-Tracks liefert in diesem Fall den typisierten Fehler
`audio_device_unavailable`.

## Audio-Policy und natives Playout

Remote-Mikrofonspuren werden zunächst nur als verfügbare Kandidaten an den
`VoiceClient` gemeldet. Seine Audio-Policy entscheidet mit Scope-Priorität,
globalen und scopebezogenen Limits, lokalem Mute/Block, Teilnehmerlautstärke
und Ducking über Admission und effektiven Gain.

Der LiveKit-Adapter verwendet deshalb kein Auto-Subscribe. Zugelassene Spuren
werden selektiv abonniert, als dekodierte PCM-Frames über `AudioStream` gelesen
und je Sprecher durch eine eigene XAudio2 Source Voice wiedergegeben. Die
vollständigen Regeln und Standardwerte stehen in
[audio-engine.md](audio-engine.md).

## Control-Plane-Client

`ControlPlaneClient` bildet den HTTP-v1-Vertrag typisiert auf Session,
Membership, Voice-Grants sowie Start und Ende einer Transmission ab. Er prüft
API-Version, Antwortschema, Geräte- und Spielerbindung, Membership-Version,
Scope, Client-Transmission-ID und Server-Transmission-ID. Fehler bleiben als
stabile `ControlPlaneError`-Werte erhalten.

`IClientHttpTransport` hält die Protokolllogik von der Plattform-HTTP-API
getrennt. Unter Windows implementiert `hvc::client_winhttp` diese Grenze mit
WinHTTP, UTF-8-Prüfung, HTTP-/HTTPS-Unterstützung, Zeitlimits,
Header-Injection-Schutz und einem begrenzten Antwortspeicher.

`AuthorizedVoiceClient` koordiniert Control Plane und Voice-Transport:

1. externe Anmeldung und gerätegebundene Session erstellen,
2. eigene autoritative Membership laden,
3. kurzlebige Grants derselben Membership-Version ausstellen,
4. ausschließlich die ausgegebenen Scope-Räume verbinden,
5. beim Drücken von PTT zuerst die Transmission serverseitig autorisieren,
6. erst nach der positiven, korrelierten Antwort das Mikrofon publizieren,
7. beim Loslassen zuerst lokal unpublishen und danach die Servertransmission
   beenden.

Schlägt die Mikrofonpublikation nach einer erfolgreichen Autorisierung fehl,
beendet der Client die Servertransmission sofort als Rollback. Bei einem
Transportabbruch kann `endInterruptedTransmission()` die bereits lokal
beendete Transmission serverseitig aufräumen.

Während einer noch ausstehenden Publikation können Release, Disconnect oder
Membership-Refresh den Start nebenläufig abbrechen. Der Starter besitzt die
Bereinigung der bereits autorisierten Servertransmission; wartende Stop-Pfade
verwenden dasselbe Ergebnis. Dadurch wird der Endpunkt auch bei konkurrierenden
Abbruchpfaden genau einmal aufgerufen.

Die abstrakte Grenze `IPushToTalkTarget` macht diesen autorisierten
PTT-Lebenszyklus für das Eingabesystem testbar. Die konkreten
Team-, Specialization- und Group-Aktionen sowie der Windows-Raw-Input-Adapter
sind in [input-system.md](input-system.md) beschrieben.

## Fehlergrenze

SDK-Ausnahmen aus Transportoperationen verlassen den Adapter nicht. Diese
Operationen liefern `VoiceTransportResult` mit einem stabilen
`VoiceTransportError`. Fehler der einmaligen SDK-Initialisierung werden beim
Erzeugen des Adapters als Exception gemeldet. Ein fehlendes
XAudio2-Wiedergabegerät bleibt als typisierter, späterer Playoutfehler erhalten,
damit reine Sender funktionieren. Asynchrone Fehler werden zusätzlich an den
registrierten Observer weitergegeben.

Observer besitzen den Transport nicht. Der Aufrufer muss sie vor ihrer
Zerstörung mit `setObserver(nullptr)` abmelden; `VoiceClient` erledigt dies
automatisch.

## Integrations-DLL

Die öffentliche Shared-Library-Grenze `hvc::client_core` kapselt
`VoiceClient` hinter der versionierten C-ABI in `hvc/client_core.h`. Ein
Host stellt den konkreten Voice-Transport über eine C-Funktionstabelle bereit;
interne HVC-, LiveKit- oder C++-Standardbibliothekstypen überschreiten die
DLL-Grenze nicht.

`hvc/client_core.hpp` ergänzt einen C++20-RAII-Wrapper mit besitzenden
Membership- und Ereigniswerten. ABI-Versionierung, Threading,
Control-Plane-Reihenfolge und Beispielintegration sind in
[client-core-integration.md](client-core-integration.md) verbindlich
dokumentiert.

## Build und Tests

Der plattformunabhängige Client-Core gehört zu allen regulären Builds und wird
als `hvc::client` exportiert.

Der native Adapter wird zusammen mit dem bestehenden Quality-Gate-Preset
gebaut:

```powershell
.\scripts\build-windows.cmd -Preset windows-msvc-livekit-quality-gate
```

Die Client-Tests verwenden Fake-HTTP-, Fake-Voice- und Fake-PTT-Ziele. Sie prüfen
Scope-Grant-Validierung, den exklusiven PTT-Lebenszyklus, die Reihenfolge
„Autorisierung vor Mikrofon“, Ablehnungen ohne Publikation, den Rollback bei
Audiofehlern und den Abbruch ohne automatische Wiederaufnahme nach einem
Reconnect. Zusätzlich prüfen sie Stream-Admission, Ducking, Mute/Block,
Teilnehmerlautstärke, Bindings, Kombinationen und die drei separaten
Eingabeaktionen. Das native Quality-Gate bleibt als unabhängiger
Ende-zu-Ende-Nachweis der verwendeten SDK-Operationen erhalten.

Die PRE-01-Regression führt zusätzlich 100 deterministisch während `starting`
abgebrochene Zyklen je Scope aus. Das native Quality-Gate wiederholt dieselbe
Anzahl gegen LiveKit Server 1.13.4, verwirft SDK-Timeouts und verspätete
Publication-Warnungen als Fehler und prüft anschließend, dass in keinem
Scope-Raum ein Mikrofontrack verbleibt.
