# LiveKit-C++-Quality-Gate

**SDK-Version:** 1.4.0  
**Zielplattform:** Windows x64

## Reproduzierbare SDK-Anbindung

Der Quality-Gate-Build lädt das offizielle vorkompilierte LiveKit-C++-SDK
`1.4.0` für Windows x64. Version und SHA-256 sind in
`cmake/HvcLiveKitSdk.cmake` festgeschrieben. Alternativ kann ein bereits
entpacktes SDK über `HVC_LIVEKIT_SDK_ROOT` verwendet werden.

Konfigurieren und bauen:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' `
  --preset windows-msvc-livekit-quality-gate
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' `
  --build --preset windows-msvc-livekit-quality-gate
```

Das notwendige `livekit_ffi.dll` wird neben
`hvc-livekit-quality-gate.exe` kopiert.

## Zwei-Client-Verbindungsprobe

Für beide Clients werden verschiedene Join-Tokens für denselben Raum benötigt.
Der erste Prozess hält seine native Verbindung für die Probe offen:

```powershell
.\out\build\windows-msvc-livekit-quality-gate\apps\livekit-quality-gate\Release\hvc-livekit-quality-gate.exe `
  --url 'ws://SERVER:7880' --token 'TOKEN_CLIENT_A' --hold 60
```

Auf dem zweiten Windows-Rechner:

```powershell
.\hvc-livekit-quality-gate.exe `
  --url 'ws://SERVER:7880' --token 'TOKEN_CLIENT_B' --wait-for-peer 60
```

Der zweite Prozess wartet auf den bereits verbundenen ersten Teilnehmer. Die
Probe ist bestanden, wenn beide Prozesse `PASS` melden. Tokens dürfen
weder versioniert noch in Testergebnisse geschrieben werden.

## Noch offene Quality-Gate-Nachweise

- Mikrofonaufnahme und Opus-Publikation/-Empfang
- paralleler Empfang der drei Scope-Räume
- PTT und Abbruch
- Reconnect ohne automatische Transmission
- Audiogerätewechsel
- serverseitiger Rechteentzug und Subscription-Isolation
