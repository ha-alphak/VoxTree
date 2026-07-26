# Windows-Client

**Stand:** 26. Juli 2026

## Anwendungsschale

Die grafische Client-Anwendung liegt unter `apps/windows-client`. Sie verwendet
WinUI 3 mit C++/WinRT und baut die Oberfläche vollständig aus C++ auf. Das
bestehende CMake-Projekt bleibt dadurch die einzige Buildquelle; ein separates,
manuell gepflegtes Visual-Studio-Projekt ist nicht erforderlich.

`main.cpp` enthält nur noch den Prozesseinstieg. `App` koordiniert den Start,
`MainWindow` die Hauptansicht und die aktive `ClientSession`. Einstellungen und
Diagnose besitzen mit `SettingsWindow` und `DiagnosticsWindow` eigene
nicht-modale Fenster. Wiederverwendbare WinUI-Helfer sind von den
UI-unabhängigen Zuständen in `hvc::presentation` getrennt. Der verbindliche
Zustands-, Befehls-, Validierungs-, Besitz- und Threadingvertrag steht in
[presentation-architecture.md](presentation-architecture.md).

Die statischen Oberflächentexte liegen als englische und deutsche
Win32-Stringtable-Ressourcen unter `apps/windows-client/resources`. Die Auswahl
erfolgt über die Windows-UI-Sprache. IDs, Gerätebezeichnungen, Rollennamen und
technische Diagnosen bleiben Laufzeitdaten und werden nicht als übersetzbare
Oberflächentexte behandelt.

## Anmeldung und Verbindungsablauf

Die Startansicht öffnet über „Verbinden“ einen lokalisierten, modalen
Windows-Dialog für:

- die HTTP- oder HTTPS-Basis-URL der Control Plane;
- ein externes Bootstrap- oder Provider-Credential.

Das Credential-Feld verwendet die native Passwortmaskierung. Das Credential
wird weder protokolliert noch im verbundenen Clientzustand gespeichert und nach
erfolgreicher Anmeldung aus dem Arbeitsspeicher der App-Schale entfernt.

Die beiden nativen Eingabefelder sind eine schmale Kompatibilitätsgrenze: In der
lokalen selbstenthaltenen Windows-App-SDK-`2.2.0`-Ausgabe lösen WinUI
`TextBox` und `PasswordBox` beim ersten Rendern reproduzierbar `E_FAIL` aus.
Andere verwendete WinUI-Steuerelemente sind davon nicht betroffen. Der
gekapselte Dialog verwendet Standard-Windows-Tabnavigation,
Accessibility-Semantik und den Systemdialogfont; die eigentliche Anwendung
bleibt WinUI 3.

Der Verbindungsablauf wird außerhalb des UI-Threads ausgeführt. Er verdrahtet in
dieser Reihenfolge:

1. `WinHttpTransport` und `ControlPlaneClient`;
2. Session-Erstellung und autoritativen Membership-Abruf;
3. Ausstellung der kurzlebigen Voice-Grants;
4. `LiveKitVoiceTransport`, `VoiceClient` und `AuthorizedVoiceClient`;
5. `AuthorizedPushToTalkInput`, `PushToTalkBindingEngine` und
   `WinRawInputSource`.

Erst wenn Control Plane, alle autorisierten Voice-Räume und Raw Input bereit
sind, wechselt die Oberfläche in den verbundenen Zustand.

## Aktuelle verbundene Ansicht

Die verbundene Ansicht verwendet eine moderne, Voice-zentrierte Fensterschale:

- eine kompakte Titelleiste zeigt Produkt und Verbindungszustand;
- die linke Seitenleiste zeigt den eigenen Spieler, öffentliche Rollen sowie
  Group, Specialization und Team als auswählbare Bereiche;
- die Hauptfläche zeigt den ausgewählten Scope, den eigenen Teilnehmer und
  aktive Remote-Sprecher dieses Scopes;
- die dauerhaft sichtbare Voice-Leiste zeigt Team-, Specialization- und
  Group-PTT samt aktueller Belegung sowie den bestätigten Sendescope;
- Einstellungen und Diagnose bleiben unabhängige, nicht-modale Fenster.

Verbindung, Senden, Empfang und letzter Fehler besitzen getrennte Textzustände.
Der Sendestatus wechselt erst nach einer erfolgreichen, korrelierten
Startautorisierung und der bestätigten lokalen LiveKit-Publikation auf
„Übertragung aktiv“. Ein Release während der noch ausstehenden Publikation
bleibt unsichtbar als aktive Übertragung, bricht dieselbe Generation ab und
beendet ihre Servertransmission genau einmal. Reconnect und Disconnect setzen
den Sendescope sichtbar zurück und nehmen eine beendete Transmission nicht
automatisch wieder auf.

Team-, Specialization- und Group-PTT sind als getrennte Aktionen sichtbar. Die
Standardbelegungen lauten weiterhin `F9`, `F10` und `F11`; alle drei
Funktionstasten können in der Eingabeansicht unabhängig neu belegt werden. Der
aktuell tatsächlich sendende Scope wird zusätzlich als Text angezeigt und ist
damit nicht ausschließlich über Farbe erkennbar.

## Aktuelle Sprecheransicht

Die Sprecheransicht wird durch strukturierte `ClientSession`-Ereignisse
aktualisiert und zeigt für jede aktive Remote-Spur:

- Scope als eindeutiges Textlabel;
- öffentliche Transport-/Spieleridentität als Name;
- ein Rollenfeld. Der aktuelle LiveKit-Ereignisvertrag liefert für entfernte
  Teilnehmer noch keine Rollenmetadaten, daher zeigt es derzeit einen
  lokalisierten Hinweis;
- den bestätigten Sprechzustand.

Pro Sprecher stehen lokale Lautstärke und lokales Mute bereit. Beide Änderungen
wirken unmittelbar über die vorhandene `VoiceClient`-Audiopolicy, verändern
aber keine serverseitige Berechtigung. Eine optional verstärkte
Sprecherhervorhebung vergrößert und betont den Textindikator.

Die eigene Membership blendet den Moderationsbereich ausschließlich bei einer
Rollen-ID `moderator`, `administrator` oder `admin` ein. Scope und Identität
aktiver Sprecher bleiben oberhalb als Entscheidungsgrundlage sichtbar.
Serverseitige Moderationsbefugnisse werden weiterhin ausschließlich von der
Control Plane geprüft.

Nicht sprechende entfernte Teilnehmer sind derzeit nicht sichtbar. Die
Hauptfläche kennzeichnet diese Grenze ausdrücklich und erfindet keine
Presence-Daten. Der eigene Membership-Endpunkt liefert keine Gruppenliste, und
Remote-Participant-Connect/-Disconnect-Ereignisse werden noch nicht bis zur
Oberfläche weitergereicht. Die geplante Kanal- und Teilnehmeransicht benötigt
deshalb zuerst den in `implementation-plan.md` beschriebenen
Directory-/Presence-Vertrag.

## Eigenständiges Einstellungsfenster

Die nachfolgend beschriebenen Steuerelemente sind technisch verdrahtet, liegen
in einem eigenständigen, nicht-modalen Einstellungsfenster. Sie werden noch
nicht persistent gespeichert; Versionierung, Migration und atomare Speicherung
folgen mit SET-01.

### Audio

Die Audioansicht liest stabile Aufnahme- und Wiedergabegeräte-IDs aus dem
LiveKit-Adapter und kann beide Geräte innerhalb der Sitzung wechseln.
Konfigurierbar sind außerdem:

- globales Streamlimit;
- getrennte Team-, Specialization- und Group-Limits;
- Team-Ducking unter Specialization und Group;
- Specialization-Ducking unter Group.

Ungültige Konfigurationen werden durch den bestehenden `VoiceClient`-Vertrag
atomar abgelehnt. Der kontrollierte Wiedergabegeräte-Reconnect stellt keine
vorherige PTT-Übertragung wieder her.

### Eingabe

Die Eingabeansicht zeigt die von Raw Input erkannten Geräteprofile und erlaubt
die unabhängige Neubelegung aller drei PTT-Aktionen mit `F1` bis `F12`.
`PushToTalkBindingEngine` prüft den gesamten Satz weiterhin atomar auf
Kollisionen. Controller-, Joystick- und HOTAS-Profile bleiben sichtbar; ihre
beliebigen HID-Buttons werden bereits vom Client-Core unterstützt und können in
einer späteren Komfortansicht direkt gewählt werden.

### Barrierefreiheit

Alle Scope-, Status- und Sprecherzustände besitzen Textlabels und verlassen
sich nicht ausschließlich auf Farbe. Die Ansicht bietet eine Textskalierung von
100 bis 150 Prozent sowie einen verstärkten Sprecherindikator. PTT bleibt
vollständig neu belegbar und Teilnehmerlautstärke unabhängig regelbar.

## Abhängigkeiten und Build

Der Client ist ein opt-in Ziel, damit die plattformübergreifenden
Standard-Builds keine UI-Abhängigkeit erhalten:

- Microsoft Windows App SDK `2.2.0`;
- Microsoft C++/WinRT `3.0.260715.1`;
- LiveKit C++ SDK `1.4.0` über die bestehende SHA-256-geprüfte Integration.

Der Post-Build-Schritt legt `livekit.dll` und `livekit_ffi.dll` zusammen mit
der Client-EXE ab. Der Debug-Build wird mit folgendem Befehl erstellt:

```powershell
.\scripts\build-windows.cmd -Preset windows-msvc-client
```

Das Ergebnis liegt unter:

```text
out/build/windows-msvc-client/apps/windows-client/Debug/hvc-windows-client.exe
```

Der Entwicklungsbuild ist eine selbstenthaltende, nicht paketierte
Windows-App-SDK-Anwendung. Das signierte MSIX-Paket bleibt Bestandteil der
Auslieferungsphase.

## Geplanter Neuaufbau

Der verbindliche offene Umfang steht im
[Umsetzungsplan vor der Auslieferungsreife](implementation-plan.md):

- Erweiterung der vorhandenen navigierbaren Hauptansicht um den vollständigen
  serverdefinierten Kanalbaum und alle sichtbaren Teilnehmer;
- getrennte Zustände für Presence, Audioverfügbarkeit und Sprechen;
- persistente, nicht geheime Benutzerkonfiguration;
- direkte Lernansicht für Tastatur, Maus und HID-/HOTAS-Buttons;
- Account-, Membership-, Rollen- und Moderationsoberflächen;
- strukturierte, rotierende Laufzeitlogs und redigierter Support-Bundle-Export;
- signierte MSIX-Paketierung erst nach den Vor-Release-Gates.
