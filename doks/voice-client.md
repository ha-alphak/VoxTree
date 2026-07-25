# Windows-Client-Core und Voice-Transport

**Stand:** 25. Juli 2026

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

## Fehlergrenze

SDK-Ausnahmen aus Transportoperationen verlassen den Adapter nicht. Diese
Operationen liefern `VoiceTransportResult` mit einem stabilen
`VoiceTransportError`. Fehler bei der einmaligen SDK- oder Audio-Initialisierung
werden beim Erzeugen des Adapters als Exception gemeldet. Asynchrone Fehler
werden zusätzlich an den registrierten Observer weitergegeben.

Observer besitzen den Transport nicht. Der Aufrufer muss sie vor ihrer
Zerstörung mit `setObserver(nullptr)` abmelden; `VoiceClient` erledigt dies
automatisch.

## Build und Tests

Der plattformunabhängige Client-Core gehört zu allen regulären Builds und wird
als `hvc::client` exportiert.

Der native Adapter wird zusammen mit dem bestehenden Quality-Gate-Preset
gebaut:

```powershell
.\scripts\build-windows.cmd -Preset windows-msvc-livekit-quality-gate
```

Die Client-Tests verwenden Fake-HTTP- und Fake-Voice-Transporte. Sie prüfen
Scope-Grant-Validierung, den exklusiven PTT-Lebenszyklus, die Reihenfolge
„Autorisierung vor Mikrofon“, Ablehnungen ohne Publikation, den Rollback bei
Audiofehlern und den Abbruch ohne automatische Wiederaufnahme nach einem
Reconnect. Das native Quality-Gate bleibt zusätzlich als unabhängiger
Ende-zu-Ende-Nachweis der verwendeten SDK-Operationen erhalten.
