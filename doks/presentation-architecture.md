# Präsentations- und Fensterarchitektur

**Stand:** 26. Juli 2026  
**Arbeitspaket:** UX-01

## Grenze

Die fachlichen Desktop-Zustände liegen in der plattformneutralen Bibliothek
`hvc::presentation` unter `libs/presentation`. Sie hängt vom ebenfalls
plattformneutralen `hvc::client`, aber weder von WinUI, Qt, Win32 noch LiveKit
ab. WinUI und der geplante Qt-Client verwenden damit dieselben:

- Verbindungs-, Kanalwahl-, Teilnehmer-, Einstellungs-, Verwaltungs- und
  Diagnosezustände;
- Befehle und Payloads;
- Validierungen;
- stabilen Fehlercodes.

Die Bibliothek enthält keine lokalisierten Texte. `ErrorCode` und
`ValidationResult::field` sind maschinenlesbare Verträge. Erst die jeweilige
Fensterschale bildet sie auf deutsche oder englische Ressourcen ab.

## Zustandsmodell

`DesktopState` ist der vollständige, UI-unabhängige Snapshot:

| Bereich | Modell |
|---|---|
| Verbindung | `signed_out`, `connecting`, `ready`, `reconnecting`, `disconnecting` |
| Kanalwahl | serverdefinierter Scope und stabile Node-ID |
| Teilnehmer | Anzeigename, Primär-Team und Rollen getrennt von Presence, Audioverfügbarkeit und Sprechen |
| Senden | ausschließlich der bestätigte aktive Sendescope |
| Einstellungen | Audio-Policy, Geräte, PTT-Bindings, Eingabegeräte und Barrierefreiheit |
| Administration | getrennte Sichtbarkeit für Moderation und Administration |
| Diagnose | Transportzustand, stabiler letzter Fehlercode, technisches Detail und Fehlerzähler |

Das Modell nimmt spätere Directory- und Presence-Daten auf, ohne deren noch
nicht spezifizierte Netzwerkverträge vorwegzunehmen. Der aktuelle
`ClientSession`-Vertrag meldet nur hörbare Sprecher. Ein Teilnehmer kann deshalb
bereits getrennte `audio_available`- und `speaking`-Zustände besitzen, wird in
der heutigen Hauptansicht aber nur während `speaking` angezeigt. Vollständige
Die serverseitigen Directory-/Presence-Daten sind mit DIR-01 verfügbar; ihre
clientseitige Transportzusammenführung folgt mit DIR-02.

Membership-Updates werden nur mit strikt höherer Version angewendet. Beim
Reconnect wird ein bestätigter Sendescope sofort gelöscht und nie automatisch
wiederhergestellt. Rollen steuern ausschließlich die sichtbaren
Verwaltungsbereiche; sie ersetzen keine Serverautorisierung.

## Befehle und Validierung

`CommandKind` definiert die gemeinsamen Shell-Befehle für Verbindung,
Kanalwahl, Fensteröffnung, Einstellungen und lokale Teilnehmerregeln.
`DesktopModel::validate()` lehnt insbesondere ab:

- Befehle außerhalb eines gültigen Verbindungszustands;
- leere Kanal- oder Teilnehmer-IDs;
- unbekannte Teilnehmer;
- Lautstärken außerhalb `0,0` bis `1,0`;
- nicht positive Streamlimits und Ducking-Werte außerhalb `0,0` bis `1,0`;
- Textskalierung außerhalb 100 bis 200 Prozent;
- ausgewählte Geräte, die nicht im aktuellen Gerätesnapshot vorkommen;
- Verwaltungsnavigation ohne sichtbare Moderator- oder Administratorrolle.

Die stabilen Fehlerwerte sind `invalid_state`, `invalid_argument`,
`not_found`, `forbidden`, `stale_version` und `operation_failed`.
Diagnosetexte von Transport- oder Plattformadaptern sind ausdrücklich kein
lokalisierter UI-Vertrag.

## Windows-Fenster

Die WinUI-Schale unter `apps/windows-client/src` ist nach Verantwortung
getrennt:

- `main.cpp` enthält nur den Prozesseinstieg;
- `App` besitzt und startet den `MainWindow`-Koordinator;
- `MainWindow` besitzt Hauptansicht, Präsentationsmodell und aktive
  `ClientSession`;
- `SettingsWindow` ist nicht modal und besitzt ausschließlich
  Einstellungssteuerelemente sowie für seine Lebensdauer eine geteilte
  Session-Referenz;
- `DiagnosticsWindow` besitzt keine Session und erhält nur den
  datensparsam begrenzten `DiagnosticsState`;
- `ViewTextRegistry` stellt wiederverwendbare lokalisierte Text-, Abschnitts-
  und Skalierungshelfer bereit.

Die Hauptansicht enthält keine eingebetteten Einstellungs- oder technischen
Diagnosedetails mehr. Sie öffnet beide eigenständigen Fenster nicht modal, damit
Verbindung und PTT weiterlaufen.

Persistente Einstellungen, vollständiges Binding-Lernen und Geräte-Hot-Plugging
in der Oberfläche gehören weiterhin zu SET-01. Strukturierte Logs,
Korrelationsansicht und Support-Bundle gehören weiterhin zu DIA-01 und DIA-02.

## Besitz und Lebensdauer

```text
App
└── shared_ptr<MainWindow>
    ├── DesktopModel (nur UI-Thread)
    ├── shared_ptr<ClientSession>
    ├── shared_ptr<SettingsWindow> (nur solange geöffnet)
    └── shared_ptr<DiagnosticsWindow>
```

`ClientSession` besitzt Control-Plane-, LiveKit-, Voice-, PTT- und
Raw-Input-Adapter. Sein Destruktor ruft `disconnect()` auf. Öffentliche
Sessionoperationen werden durch `services_mutex_` serialisiert. Beim Trennen
wird zuerst der Membership-Worker gestoppt und gejoint; danach werden Raw Input
und Observer in umgekehrter Besitzreihenfolge gelöst.

Ein Einstellungsfenster darf eine Session während einer bereits gestarteten
Hintergrundoperation kurzfristig am Leben halten. `disconnect()` wartet durch
dieselbe Serialisierung auf diese Operation und baut anschließend die Services
geordnet ab.

## Threads und Dispatcher

- `DesktopModel` und alle WinUI-Controls werden ausschließlich auf dem
  Dispatcher-Thread gelesen oder verändert.
- Anmeldung, Sessionaufbau, Trennen, Audiogerätewechsel sowie lokale
  Teilnehmer-Audiooperationen laufen auf Worker-Threads.
- `ClientSession`-Callbacks dürfen von Transport-, Audio-, Raw-Input- oder
  Membership-Threads eintreffen. Sie blockieren dort nicht, sondern werden
  über `DispatcherQueue::TryEnqueue()` an die Oberfläche übergeben.
- Jede Session erhält eine steigende Generation. Bereits eingereihte Callbacks
  einer getrennten oder ersetzten Session werden verworfen.
- Dispatcher-Lambdas und Window-Events halten nur `weak_ptr` auf ihre
  Koordinatoren. Coroutines halten während ihrer Arbeit bewusst eine starke
  Referenz und prüfen nach dem Threadwechsel zusätzlich Fenster- und
  Sessiongeneration.
- Häufige Lautstärkeänderungen werden 150 ms zusammengefasst und
  serialisiert im Hintergrund angewendet. Dadurch blockiert kein LiveKit-Aufruf
  den UI-Thread und ältere Batches können neuere Werte nicht überholen.

## Lokalisierung und Nachweise

Alle statischen WinUI-Texte bleiben in der englischen und deutschen
Stringtable. Ein CMake-Pre-Build-Gate vergleicht beide ID-Mengen und bricht den
Clientbuild bei einer Abweichung ab.

`presentation.desktop_model` prüft ohne WinUI:

- Verbindungs- und Membership-Lebenszyklus einschließlich veralteter Versionen;
- getrennte Teilnehmer-, Audio- und Sprechzustände;
- Rollen- und Verwaltungsanzeige ohne Teilstring-Freigaben;
- Befehls-, Geräte-, Audio- und Barrierefreiheitsvalidierung;
- sitzungsgebundene Diagnosezustände.
