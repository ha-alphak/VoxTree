# Windows-Client

**Stand:** 26. Juli 2026

## Umfang der ersten Client-Schale

Die erste grafische Client-Anwendung liegt unter `apps/windows-client`. Sie
verwendet WinUI 3 mit C++/WinRT und baut die Oberfläche zunächst vollständig aus
C++ auf. Dadurch bleibt das bestehende CMake-Projekt die einzige Buildquelle,
ohne ein separates, manuell gepflegtes Visual-Studio-Projekt einzuführen.

Die Server- und Anmeldeansicht nimmt entgegen:

- die HTTP- oder HTTPS-Basis-URL der Control Plane;
- ein externes Bootstrap- oder Provider-Credential.

Der Verbindungsablauf wird außerhalb des UI-Threads ausgeführt. Er verdrahtet in
dieser Reihenfolge:

1. `WinHttpTransport` und `ControlPlaneClient`;
2. Session-Erstellung und autoritativen Membership-Abruf;
3. Ausstellung der kurzlebigen Voice-Grants;
4. `LiveKitVoiceTransport`, `VoiceClient` und `AuthorizedVoiceClient`;
5. `AuthorizedPushToTalkInput`, `PushToTalkBindingEngine` und
   `WinRawInputSource`.

Erst wenn Control Plane, alle autorisierten Voice-Räume und Raw Input bereit
sind, wechselt die Oberfläche in den verbundenen Bereitschaftszustand. Dort
werden die Membership-IDs und die vorläufigen Standardbindings angezeigt:

| Scope | Taste |
|---|---|
| Team | `F9` |
| Specialization | `F10` |
| Group | `F11` |

Das Credential wird nach erfolgreicher Anmeldung aus dem Eingabefeld entfernt
und weder protokolliert noch im Clientzustand gespeichert. PTT verwendet
weiterhin den bestehenden autorisierten Ablauf „Control Plane vor Mikrofon“.

## Abhängigkeiten und Build

Der Client ist ein opt-in Ziel, damit die plattformübergreifenden
Standard-Builds keine UI-Abhängigkeit erhalten. Die Versionen sind im
Client-CMake-Ziel festgelegt:

- Microsoft Windows App SDK `2.2.0`;
- Microsoft C++/WinRT `3.0.260715.1`;
- LiveKit C++ SDK `1.4.0` über die bestehende SHA-256-geprüfte Integration.

Der Post-Build-Schritt legt `livekit.dll` und `livekit_ffi.dll` zusammen mit
der Client-EXE ab. Beide Dateien sind Laufzeitbestandteile des festgelegten
LiveKit-SDK-Archivs und müssen beim Start über das Anwendungsverzeichnis
auflösbar sein.

Die lokal wiederhergestellten Originalpakete wurden auf ihre Lizenzdateien
geprüft. C++/WinRT steht unter der MIT-Lizenz. Die Microsoft-Softwarelizenz des
Windows App SDK erlaubt Entwicklung und Test unter Windows sowie die
Weitergabe der vom NuGet-Paket neben der Anwendung abgelegten
Laufzeitkomponenten. Beide Pakete stammen aus dem offiziellen
Microsoft-Namespace auf NuGet; Versionsbereiche werden nicht verwendet.

Debug-Build:

```powershell
.\scripts\build-windows.cmd -Preset windows-msvc-client
```

Das Ergebnis liegt unter:

```text
out/build/windows-msvc-client/apps/windows-client/Debug/hvc-windows-client.exe
```

Der Entwicklungsbuild ist eine selbstenthaltende, nicht paketierte
Windows-App-SDK-Anwendung. Das signierte MSIX-Paket bleibt Bestandteil der
späteren Auslieferungsphase.

## Noch offen

- stabile lokale Geräteidentität und persistente, nicht geheime
  Servereinstellungen;
- Membership- und Voice-Grant-Refresh bei Ablauf oder Reconnect;
- Hierarchie-, Sprecher-, Audio- und Eingabeeinstellungen;
- lokalisierbare Ressourcen anstelle direkt gesetzter UI-Texte;
- signierte MSIX-Paketierung.
