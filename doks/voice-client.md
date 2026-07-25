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

`VoiceClient` prüft, dass genau ein nicht leerer Grant pro Scope vorliegt. Er
verhindert parallele PTT-Übertragungen und verwirft seinen aktiven PTT-Zustand
bei jedem Reconnect oder Disconnect. Eine beendete Übertragung wird nach einem
Reconnect niemals automatisch fortgesetzt.

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

Die Client-Tests verwenden einen Fake-Transport. Sie prüfen die vollständige
Scope-Grant-Validierung, den exklusiven PTT-Lebenszyklus und den Abbruch ohne
automatische Wiederaufnahme nach einem Reconnect. Das native Quality-Gate
bleibt zusätzlich als unabhängiger Ende-zu-Ende-Nachweis der verwendeten
SDK-Operationen erhalten.
