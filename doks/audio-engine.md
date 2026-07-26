# Audio-Engine

**Stand:** 26. Juli 2026

## Schichtengrenze

Die Audio-Policy liegt im UI-unabhängigen `VoiceClient`. Sie entscheidet vor
dem Dekodieren einer veröffentlichten Remote-Mikrofonspur über:

- Stream-Admission mit einem globalen und je Scope getrennten Limit;
- lokales Mute und Block;
- individuelle lineare Teilnehmerlautstärke;
- hierarchisches Ducking.

Der Transport meldet eine Spur zunächst als verfügbar. Erst die atomare
Playback-Anweisung aus `admitted` und effektivem Gain darf sie abonnieren,
dekodieren und wiedergeben. Eine nicht zugelassene Spur verbraucht daher keinen
lokalen Decoder. Die Entscheidung verändert ausschließlich lokales Playout und
niemals serverseitige Membership oder Empfängerberechtigungen.

## Stream-Admission

Die Priorität ist verbindlich:

1. Group
2. Specialization
3. Team

Innerhalb eines Scopes gilt eine stabile First-Come-Reihenfolge. Neue
höherpriorisierte Spuren verdrängen die zuletzt nachrangigen Spuren, sobald das
globale Limit erreicht ist. Endet, blockiert oder mutet der Benutzer eine
zugelassene Spur, wird der nächste wartende Kandidat deterministisch
nachgezogen.

Die Standardwerte entsprechen den Architekturentscheidungen:

| Grenze | Standard |
|---|---:|
| Gleichzeitig dekodierte Streams | 8 |
| Team | 5 |
| Specialization | 4 |
| Group | 2 |

Alle Grenzen müssen positiv sein. Ungültige Konfigurationen werden abgelehnt,
ohne den vorherigen Zustand zu verändern.

## Ducking und Teilnehmerlautstärke

Der effektive Gain ist das Produkt aus individueller Teilnehmerlautstärke und
dem zutreffenden Ducking-Faktor. Standardmäßig gelten:

| Aktive höhere Priorität | Betroffener Scope | Gain |
|---|---|---:|
| Specialization | Team | 0,50 |
| Group | Team | 0,25 |
| Group | Specialization | 0,50 |

Group wird nicht geduckt. Ist Group aktiv, wird für Team ausschließlich der
Group-Faktor verwendet; die Faktoren werden nicht mehrfach kaskadiert. Alle
Werte sind lineare Faktoren zwischen `0,0` und `1,0` und zur Laufzeit
konfigurierbar.

Individuelle Lautstärke liegt ebenfalls zwischen `0,0` und `1,0`. Mute und
Block sind getrennte lokale Zustände, führen aber beide unmittelbar zu
`admitted=false` und Gain `0,0`. Eine Änderung während einer laufenden
Transmission betrifft nur den lokalen Empfänger und lässt andere Empfänger
unverändert.

## Nativer Windows-Pfad

Der LiveKit-Adapter verbindet die Räume mit deaktiviertem Auto-Subscribe. Eine
zugelassene Opus-Mikrofonspur wird selektiv abonniert und über LiveKits
dekodierten `AudioStream` an eine eigene XAudio2 Source Voice übergeben. Der
berechnete Gain liegt direkt an dieser Source Voice an; dadurch wirken Ducking
und individuelle Lautstärke unabhängig pro Sprecher.

Die Playout-Worker sind von UI- und Netzwerk-Callbacks getrennt. Der
`AudioStream` begrenzt seinen Jitter-Puffer auf acht Frames, XAudio2 höchstens
zwölf ausstehende Frames je Source Voice. Abbestellen, Unpublish, Disconnect
und Teilnehmerabgang schließen den Stream, stoppen den Worker und geben die
native Voice frei. Der bekannte kontrollierte Reconnect beim Wechsel des
Wiedergabegeräts stellt zusätzlich das XAudio2-Mastering-Voice auf die gewählte
stabile Geräte-ID um.

Mikrofonaufnahme, AEC, Noise Suppression, AGC, Opus-Publikation und
Aufnahmegerätewechsel bleiben im bereits validierten
`PlatformAudio`-Aufnahmepfad. Die neue lokale Policy erhält weder
Grant-Tokens noch interne serverseitige Empfängerlisten.

## Automatisierte Nachweise

`client.voice_client` prüft:

- globale und scopebezogene Grenzen;
- deterministische Verdrängung durch höherpriorisierte Spuren;
- die drei Ducking-Beziehungen;
- individuelle Lautstärke;
- sofortiges Mute und Block während verfügbarer Audioübertragungen;
- deterministische Wiederzulassung wartender Spuren;
- Eingabevalidierung und Entfernen beendeter Veröffentlichungen.

Der opt-in LiveKit-Build kompiliert zusätzlich den selektiven
Subscription-, `AudioStream`- und XAudio2-Pfad gegen das festgelegte native SDK
1.4.0.
