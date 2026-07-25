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

## Reconnect ohne automatische Transmission

Die Reconnect-Probe beginnt mit einer kurzen PTT-Publikation und beendet diese
vollständig, bevor die Verbindungsunterbrechung ausgelöst wird:

```powershell
.\out\build\windows-msvc-livekit-quality-gate\apps\livekit-quality-gate\Release\hvc-livekit-quality-gate.exe `
  --url 'ws://127.0.0.1:7880' --token 'TOKEN_SENDER' `
  --ptt 3 --expect-reconnect --wait-for-peer 60
```

Sobald die Probe `Waiting ... for a disconnect and successful reconnect`
ausgibt, wird der Testserver hart beendet und innerhalb des Zeitlimits mit
derselben Adresse und denselben Entwicklungsschlüsseln neu gestartet.
Verbindungsfehler und Wiederholungsversuche im SDK-Log sind während dieses
absichtlichen Ausfalls erwartet.

Die Probe verlangt ein vollständiges `onReconnecting`-/`onReconnected`-
Ereignispaar, den Zustand `Connected` nach der Wiederherstellung und eine
weiterhin leere lokale Track-Publikationsliste. Sie meldet nur dann `PASS`,
wenn die vor dem Ausfall beendete PTT-Transmission nicht automatisch
fortgesetzt wurde.

## Audiogerätewechsel während der Sitzung

Ausgangs- und Zielgeräte werden über die stabilen IDs aus
`--list-audio-devices` angegeben. Der Sender wechselt das Aufnahmegerät bei
laufender Opus-Publikation:

```powershell
.\hvc-livekit-quality-gate.exe `
  --url 'ws://SERVER:7880' --token 'TOKEN_SENDER' --publish-audio `
  --recording-device 'MIKROFON_ID_A' `
  --switch-recording-device 'MIKROFON_ID_B' `
  --switch-after 3 --hold 15
```

Der Empfänger wechselt parallel das Wiedergabegerät:

```powershell
.\hvc-livekit-quality-gate.exe `
  --url 'ws://SERVER:7880' --token 'TOKEN_RECEIVER' --expect-audio `
  --playout-device 'AUSGABE_ID_A' `
  --switch-playout-device 'AUSGABE_ID_B' `
  --switch-after 3 --hold 12
```

LiveKit-C++-SDK 1.4.0 akzeptiert den Aufnahmegerätewechsel bei laufender
Publikation. Einen direkten Wiedergabegerätewechsel lehnt das SDK dagegen bei
aktivem Playout mit `Device not found` ab, obwohl dasselbe Zielgerät vor dem
Playout auswählbar ist. Die Probe verwendet deshalb einen kontrollierten
Reconnect des autorisierten Raums: Raum trennen, Zielgerät setzen und mit
demselben raumgebundenen Token erneut verbinden. Die Anwendungssitzung bleibt
dabei erhalten.

`PASS` verlangt nach dem Wechsel einen verbundenen Raum sowie die weiterhin
aktive lokale Opus-Publikation beziehungsweise das erneut aktive
Remote-Opus-Abonnement. Ausgangs- und Ziel-ID müssen verschieden und die
`--hold`-Dauer größer als `--switch-after` sein.

## Rechteentzug und Autorisierungsgrenzen

Die Sicherheitsprobe ist als wiederholbares PowerShell-Skript hinterlegt. Sie
erzeugt kurzlebige Tokens ausschließlich im Arbeitsspeicher und schreibt weder
Tokens noch das API-Secret in die Testausgabe:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\Invoke-LiveKitSecurityQualityGate.ps1
```

Standardmäßig verwendet das Skript den lokalen LiveKit-Server 1.13.4 mit den
Entwicklungs-Credentials `devkey`/`secret`. Falls Port `7880` frei ist, startet
es den in `ToolChains.md` angegebenen Server selbst und beendet ausschließlich
diesen eigenen Prozess im `finally`-Block. Für einen bereits laufenden,
abweichend konfigurierten Server können `-Url`, `-ApiKey` und `-ApiSecret`
übergeben werden.

Die Probe führt drei voneinander getrennte Nachweise:

1. Ein Sender veröffentlicht einen Opus-Mikrofontrack. Danach entzieht
   `RoomService.UpdateParticipant` serverseitig `can_publish`. `PASS` verlangt
   in der autoritativen Antwort `can_publish=false`, eine leere Trackliste und
   `is_publisher=false` innerhalb von höchstens zwei Sekunden. Der verbundene
   Empfänger muss außerdem erst das Opus-Abonnement und anschließend dessen
   Entfernung sehen.
2. Ein Token mit `can_subscribe=false` verbindet sich mit demselben Raum wie
   ein aktiver Opus-Sender. Die Admin-API bestätigt den vorhandenen Sendertrack;
   der native Empfänger sieht den Teilnehmer während des gesamten
   Beobachtungsfensters, erhält aber kein Track-Abonnement.
3. Empfänger und aktiver Opus-Sender verbinden sich mit unterschiedlich
   benannten, jeweils im Token festgelegten Räumen. Der Empfänger darf weder
   den fremden Teilnehmer noch dessen Track sehen.

LiveKit-C++-SDK 1.4.0 entfernt nach dem administrativen Rechteentzug den
veralteten lokalen Publication-Handle des Senders nicht zuverlässig aus seiner
eigenen C++-Ansicht. Der Sicherheitsnachweis stützt sich deshalb auf die
autoritative Serverantwort und das tatsächlich am Remote-Empfänger eintretende
Unpublish. Der Server stoppt die Auslieferung unmittelbar; der Anwendungskern
beendet bei einem Rechtewechsel zusätzlich seine lokale Aufnahme über den
bereits vorhandenen Transmissionszustandsautomaten.

## Überführung in den Client

Die validierten Raum-, Mikrofon-, PTT-, Reconnect- und Gerätewechseloperationen
sind in `hvc::livekit::LiveKitVoiceTransport` hinter die transportneutrale
`IVoiceTransport`-Schnittstelle überführt. Das Probeprogramm bleibt als
unabhängiger nativer Regressionstest bestehen und baut den Adapter im selben
opt-in Preset mit.

## Quality-Gate-Ergebnis

Alle nicht ausdrücklich übersprungenen Nachweise sind bestanden. Der
ursprünglich geforderte Mikrofon-/Opus-Test auf zwei physischen Windows-Rechnern
ist gemäß Projektentscheidung übersprungen; die entsprechenden lokalen
Mehrprozess-Nachweise sind bestanden. LiveKit bleibt damit der ausgewählte
Voice-Transport.
