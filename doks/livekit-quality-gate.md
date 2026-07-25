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

## Audiogeräte

Die verfügbaren Ein- und Ausgabegeräte können ohne Serververbindung aufgelistet
werden:

```powershell
.\out\build\windows-msvc-livekit-quality-gate\apps\livekit-quality-gate\Release\hvc-livekit-quality-gate.exe `
  --list-audio-devices
```

Standardmäßig verwendet die Probe die Windows-Standardgeräte. Ein bestimmtes
Gerät kann über die bei der Auflistung ausgegebene stabile ID mit
`--recording-device 'ID'` beziehungsweise `--playout-device 'ID'` ausgewählt
werden.

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

## Mikrofon- und Opus-Probe

Der native Plattform-Audiopfad des SDK übernimmt Aufnahme und Wiedergabe. Für
die Mikrofonaufnahme sind Echo Cancellation, Noise Suppression und Automatic
Gain Control aktiviert. LiveKit veröffentlicht die Aufnahme als
`audio/opus`-Track mit maximal 64 kbit/s. RED bleibt für diesen expliziten
Codec-Nachweis deaktiviert.

Zuerst auf dem empfangenden Rechner starten:

```powershell
.\out\build\windows-msvc-livekit-quality-gate\apps\livekit-quality-gate\Release\hvc-livekit-quality-gate.exe `
  --url 'ws://SERVER:7880' --token 'TOKEN_RECEIVER' `
  --expect-audio --hold 60
```

Danach auf dem sendenden Rechner starten und in das Mikrofon sprechen:

```powershell
.\hvc-livekit-quality-gate.exe `
  --url 'ws://SERVER:7880' --token 'TOKEN_SENDER' `
  --publish-audio --hold 30
```

Die Senderprobe ist bestanden, wenn Mikrofonaufnahme und Opus-Publikation für
die angegebene Dauer aktiv bleiben. Die Empfängerprobe verlangt zusätzlich
einen abonnierten Mikrofon-Track mit MIME-Typ `audio/opus` und hält das native
Lautsprecher-Playout während der Probe aktiv. Für einen lokalen Test auf einem
Rechner werden Kopfhörer empfohlen, um Rückkopplung zu vermeiden.

Für eine bidirektionale Probe können beide Prozesse mit `--publish-audio` und
`--expect-audio` gestartet werden. Die Tokens müssen dann sowohl Publish- als
auch Subscribe-Rechte enthalten.

## Parallele Drei-Scope-Empfangsprobe

Der Empfänger kann gleichzeitig je eine Verbindung zum Team-,
Specialization- und Group-Raum halten. Dafür werden drei getrennte, auf den
jeweiligen Raum beschränkte Join-Tokens übergeben:

```powershell
.\out\build\windows-msvc-livekit-quality-gate\apps\livekit-quality-gate\Release\hvc-livekit-quality-gate.exe `
  --url 'ws://SERVER:7880' `
  --team-token 'TOKEN_TEAM_RECEIVER' `
  --specialization-token 'TOKEN_SPECIALIZATION_RECEIVER' `
  --group-token 'TOKEN_GROUP_RECEIVER' `
  --expect-audio --wait-for-peer 60
```

In jedem der drei Räume muss gleichzeitig ein anderer Teilnehmer verbunden
sein. Mit `--expect-audio` verlangt die Probe zusätzlich in jedem Raum einen
abonnierten Mikrofon-Track mit MIME-Typ `audio/opus`. Die Probe meldet erst
`PASS`, wenn alle drei Scope-Bedingungen gleichzeitig erfüllt sind; bei einem
Timeout werden die fehlgeschlagenen Scopes einzeln ausgegeben.

Zum Senden können drei Instanzen der vorhandenen Ein-Raum-Probe mit je einem
raumgebundenen Publish-Token gestartet werden. Der lokale Nachweis gegen
LiveKit Server 1.13.4 wurde mit einem Drei-Scope-Empfänger und drei parallelen
Mikrofon-/Opus-Sendern erbracht.

## PTT-Start und sauberer Abbruch

Die PTT-Probe veröffentlicht das Mikrofon nur für die mit `--ptt` angegebene
Dauer. Beim Loslassen wird der Track explizit unpubliziert, während die
LiveKit-Raumverbindung bestehen bleibt:

```powershell
.\out\build\windows-msvc-livekit-quality-gate\apps\livekit-quality-gate\Release\hvc-livekit-quality-gate.exe `
  --url 'ws://SERVER:7880' --token 'TOKEN_RECEIVER' `
  --expect-ptt --wait-for-peer 30
```

Danach den Sender starten:

```powershell
.\hvc-livekit-quality-gate.exe `
  --url 'ws://SERVER:7880' --token 'TOKEN_SENDER' --ptt 5
```

Der Sender meldet nur dann `PASS`, wenn die Mikrofonpublikation entfernt wurde
und der Raum danach weiterhin verbunden ist. Der Empfänger verlangt in dieser
Reihenfolge einen abonnierten `audio/opus`-Mikrofontrack und dessen
anschließendes Unpublish/Unsubscribe; eine getrennte Senderverbindung gilt
nicht als sauberer PTT-Abbruch.

Der ursprünglich vorgesehene Mikrofon-/Opus-Nachweis auf zwei getrennten
Windows-Rechnern wird gemäß Projektentscheidung übersprungen. Die lokalen
Zwei-Prozess-, Drei-Scope- und PTT-Nachweise bleiben verbindlich.

## Noch offene Quality-Gate-Nachweise

- Reconnect ohne automatische Transmission
- Audiogerätewechsel
- serverseitiger Rechteentzug und Subscription-Isolation
