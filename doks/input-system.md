# Windows-Eingabesystem

**Stand:** 26. Juli 2026

## Schichtengrenzen

Das Eingabesystem besteht aus zwei Bibliotheksebenen:

- `hvc::client` enthält die plattformunabhängigen PTT-Aktionen, Bindings,
  Zustandsaggregation und die Weiterleitung an den autorisierten Voice-Client.
- `hvc::client_win_raw_input` erfasst Tastatur-, Maus- und generische
  Controllerbuttons über Win32 Raw Input und liefert normalisierte Ereignisse
  an `IInputEventSink`.

Die vollständige Kette lautet:

`WinRawInputSource` → `PushToTalkBindingEngine` →
`AuthorizedPushToTalkInput` → `AuthorizedVoiceClient`.

Team, Specialization und Group sind eigenständige `PushToTalkAction`-Werte.
`voiceScopeFor()` bildet sie ohne implizit ausgewählten Standardscope auf die
gleichnamigen Voice-Scopes ab.

## Bindings

Ein `InputBinding` ordnet genau eine Aktion einer Tastenkombination zu. Pro
Aktion dürfen mehrere alternative Bindings existieren. Der Binding-Kern
unterstützt:

- Einzelbelegungen und Kombinationen mehrerer Tasten,
- alternative Tastatur- und Mausbelegungen pro Aktion,
- Buttons von Gamepads, Joysticks und HOTAS-Geräten,
- geräteübergreifende oder über `device_id` auf ein Gerät begrenzte Belegungen,
- idempotente Behandlung von Tastatur-Autorepeat,
- Aggregation alternativer Belegungen, sodass die Aktion erst nach Freigabe
  aller aktiven Alternativen endet,
- Freigabe gehaltener Eingaben beim Entfernen eines Geräts,
- atomaren Austausch der Konfiguration,
- Fehler für leere, ungültige, doppelte und zwischen Aktionen kollidierende
  Bindings.

Ein leerer `device_id` in der Konfiguration bedeutet „jedes passende Gerät“.
Die Konfliktprüfung berücksichtigt das als Platzhalter: Eine allgemeine
Belegung und dieselbe gerätespezifische Belegung dürfen daher nicht
verschiedenen Aktionen zugeordnet werden.
Ereignisse des Raw-Input-Adapters tragen den von Windows gelieferten
Gerätepfad. `usage_page` und `code` identifizieren HID-Buttons anhand ihrer
Usage Page und Usage. `extended` unterscheidet bei Tastaturereignissen unter anderem
erweiterte Varianten wie Nummernblock-Enter. Für Tastaturen ist `code` der
Win32-Virtual-Key-Code. Für Mäuse verwendet `MouseButton` stabile Werte für
links, rechts, Mitte sowie Taste 4 und 5.

`InputDeviceProfile` hält die getrennte Geräteidentität, Anzeigename,
Geräteklasse, Vendor-/Product-ID, Top-Level-Usage und alle vom HID-Parser
gemeldeten Button-Usages. Der Binding-Kern aktualisiert diese Profile bei
Anschluss und entfernt sie zusammen mit gehaltenen Controls beim Hot-Unplug.
Damit kann die spätere Oberfläche Bindings eindeutig einem physischen Gerät
zuordnen und getrennte Profile anzeigen.

`AuthorizedPushToTalkInput` hält höchstens eine PTT-Aktion aktiv. Ein zweiter
Scope wird nicht gestartet, solange die erste Aktion gedrückt ist. Ergebnisse
der Autorisierung und der Voice-Publikation werden über
`IPushToTalkInputObserver` weitergegeben.

## Win32 Raw Input

`WinRawInputSource` besitzt ein unsichtbares, nicht aktivierbares
Top-Level-Fenster (`WS_EX_NOACTIVATE`) auf einem eigenen Thread. Beim Start
registriert es die Generic-Desktop-Usages für Tastatur, Maus, Joystick, Gamepad
und Multi-Axis-Controller mit:

- `RIDEV_INPUTSINK` für den Empfang bei fremdem Vordergrundfenster,
- `RIDEV_DEVNOTIFY` für das Entfernen von Geräten.

Der Adapter setzt weder `RIDEV_NOLEGACY` noch globale Hooks ein. Er simuliert,
verändert oder unterdrückt daher keine Eingabe des aktiven Spiels.

Die Raw-Input-Gerätekennung wird aus `RIDI_DEVICENAME` als UTF-8 übernommen und
pro Gerätehandle zwischengespeichert. Bereits vorhandene Geräte werden beim
Start über `GetRawInputDeviceList` inventarisiert; `GIDC_ARRIVAL` ergänzt
Hot-Plug-Geräte. Bei `GIDC_REMOVAL` wird die Kennung an den
Binding-Kern gemeldet, damit keine PTT-Aktion durch ein abgezogenes Gerät
gedrückt bleibt. Mausbewegung und Mausrad werden absichtlich ignoriert.

Generische HID-Controller werden über `RIDI_PREPARSEDDATA`, `HidP_GetCaps` und
`HidP_GetButtonCaps` beschrieben. Für jeden Eingabereport ermittelt
`HidP_GetUsages` die gedrückten Buttons. Der Adapter verwaltet den Zustand pro
Report-ID und gibt nur tatsächliche Press-/Release-Änderungen weiter. Achsen,
Hats und Ausgabereports beeinflussen PTT nicht.

`start()` und `stop()` sind wiederholt aufrufbar. Der Destruktor beendet den
Nachrichtenthread. Der Ereignis-Sink muss bis nach dem Stoppen der Quelle
existieren.

Jedes `InputEvent` enthält zusätzlich `received_in_background`. Dieser Wert
stammt direkt aus `GET_RAWINPUT_CODE_WPARAM`: Nur `RIM_INPUTSINK` gilt als
Fremdfokus-Nachweis. Prozess- oder Konsolenfenster-IDs werden dafür nicht
heuristisch verglichen. `RawInputStatistics` stellt die Anzahl empfangener
`WM_INPUT`-Nachrichten und ausgelieferter Controls für Diagnosen bereit.

## Tests und Hardware-Quality-Gate

Die plattformunabhängigen Tests prüfen die drei Aktionen, Kombinationen,
Alternativbelegungen, Gerätefilter, Autorepeat, Hot-Unplug, HID-Geräteprofile,
HID-Buttonbindings, Konflikte und die exklusive autorisierte PTT-Weiterleitung.

Der Windows-Test startet und stoppt das versteckte Raw-Input-Fenster samt
Registrierung mehrfach. Ein manueller Nachweis mit realer Tastatur
und Maus, während ein anderes Programm den Fokus besitzt, bleibt Teil des
späteren Windows-Client-Integrationslaufs, weil CTest keine physische
Benutzereingabe erzeugen soll.

Für diesen Nachweis wird `hvc-input-quality-gate` in regulären Windows-Builds
erzeugt. Die Probe listet Geräteprofile und reale Inputereignisse auf und
prüft über das aktuelle Vordergrundfenster, dass die Quelle nicht dessen
Prozess ist:

```powershell
.\out\build\windows-msvc\apps\input-quality-gate\Release\hvc-input-quality-gate.exe `
  --observe 30 --require-controller --require-background-controller-event
```

Auf der lokalen Entwicklungsmaschine wurden drei Joystick/HOTAS-Collections
mit VID `0x3344` erkannt. Zwei davon melden 32 beziehungsweise 72 Buttons.
Am 26. Juli 2026 wurde eine reale HOTAS-/Joysticktaste bei fremdem
Vordergrundfenster empfangen. Die Probe bestätigte sowohl den
Controllerbutton als auch dessen `RIM_INPUTSINK`-Kennzeichnung. Damit ist der
physische HID-Hintergrundnachweis bestanden.
