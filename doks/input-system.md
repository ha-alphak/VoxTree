# Windows-Eingabesystem

**Stand:** 25. Juli 2026

## Schichtengrenzen

Das Eingabesystem besteht aus zwei Bibliotheksebenen:

- `hvc::client` enthält die plattformunabhängigen PTT-Aktionen, Bindings,
  Zustandsaggregation und die Weiterleitung an den autorisierten Voice-Client.
- `hvc::client_win_raw_input` erfasst Tastatur- und Maustasten über Win32 Raw
  Input und liefert normalisierte Ereignisse an `IInputEventSink`.

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
Gerätepfad. `extended` unterscheidet bei Tastaturereignissen unter anderem
erweiterte Varianten wie Nummernblock-Enter. Für Tastaturen ist `code` der
Win32-Virtual-Key-Code. Für Mäuse verwendet `MouseButton` stabile Werte für
links, rechts, Mitte sowie Taste 4 und 5.

`AuthorizedPushToTalkInput` hält höchstens eine PTT-Aktion aktiv. Ein zweiter
Scope wird nicht gestartet, solange die erste Aktion gedrückt ist. Ergebnisse
der Autorisierung und der Voice-Publikation werden über
`IPushToTalkInputObserver` weitergegeben.

## Win32 Raw Input

`WinRawInputSource` besitzt ein unsichtbares Message-only-Fenster auf einem
eigenen Thread. Beim Start registriert es die HID-Usage-Werte für Tastatur und
Maus mit:

- `RIDEV_INPUTSINK` für den Empfang bei fremdem Vordergrundfenster,
- `RIDEV_DEVNOTIFY` für das Entfernen von Geräten.

Der Adapter setzt weder `RIDEV_NOLEGACY` noch globale Hooks ein. Er simuliert,
verändert oder unterdrückt daher keine Eingabe des aktiven Spiels.

Die Raw-Input-Gerätekennung wird aus `RIDI_DEVICENAME` als UTF-8 übernommen und
pro Gerätehandle zwischengespeichert. Bei `GIDC_REMOVAL` wird sie an den
Binding-Kern gemeldet, damit keine PTT-Aktion durch ein abgezogenes Gerät
gedrückt bleibt. Mausbewegung und Mausrad werden absichtlich ignoriert.

`start()` und `stop()` sind wiederholt aufrufbar. Der Destruktor beendet den
Nachrichtenthread. Der Ereignis-Sink muss bis nach dem Stoppen der Quelle
existieren.

## Tests und verbleibender Hardware-Nachweis

Die plattformunabhängigen Tests prüfen die drei Aktionen, Kombinationen,
Alternativbelegungen, Gerätefilter, Autorepeat, Hot-Unplug, Konflikte und die
exklusive autorisierte PTT-Weiterleitung.

Der Windows-Test startet und stoppt das Message-only-Fenster samt
Raw-Input-Registrierung mehrfach. Ein manueller Nachweis mit realer Tastatur
und Maus, während ein anderes Programm den Fokus besitzt, bleibt Teil des
späteren Windows-Client-Integrationslaufs, weil CTest keine physische
Benutzereingabe erzeugen soll.
