# Reproduzierbare Last- und Sicherheitstests

**Stand:** 26. Juli 2026

## Ziel und Testmodell

Der Test schließt Abschnitt 10.2 des Umsetzungsplans ab. Er erzeugt die Last
headless auf wenigen Linux-Rechnern; reale Windows-Clients ergänzen nur die
transportnahe Sicherheitsprüfung.

`hvc-load-driver prepare` legt eine deterministische SQLite-Fixture und eine
Identity-Datei an:

- 200 unterschiedliche Spieler in einer gemeinsamen Group,
- 40 Teams mit jeweils fünf Spielern,
- vier Specializations,
- getrennte Sende-, Empfangs-, Moderator- und Administratorrollen,
- atomar geprüfte Sprecherlimits,
- zwei zusätzliche, vollständig isolierte Sicherheitsidentitäten.

Damit laufen 202 unterschiedliche Spieler, gerätegebundene Sessions und
Memberships. Die beiden Sicherheitsidentitäten gehören ausdrücklich nicht zur
200-Empfänger-Group.

## Reproduzierbarer Lauf

Der vollständige Lauf benötigt Docker mit Compose auf Debian 13:

```sh
HVC_LOAD_ADVERTISE_IP=192.0.2.10 \
  sh ./scripts/run-load-and-security-tests.sh
```

`HVC_LOAD_ADVERTISE_IP` ist die vom ergänzenden Windows-Rechner erreichbare
IPv4-Adresse. Ohne externen Client wird die erste Host-Adresse verwendet.
Optionale Parameter sind:

| Variable | Standard | Zweck |
|---|---:|---|
| `HVC_LOAD_GROUP_PLAYERS` | `200` | Anzahl berechtigter Group-Empfänger |
| `HVC_LOAD_MEDIA_DURATION` | `20s` | Dauer der einzelnen Medienphasen |
| `HVC_LOAD_RESULT_DIRECTORY` | `out/load-tests/<UTC-Zeit>` | Ergebnisordner |
| `HVC_LOAD_PROJECT` | zeitbasierter Name | isolierter Compose-Projektname |

Das Skript baut `deploy/load-generator.Dockerfile` und verwendet den offiziellen
LiveKit-Lastgenerator `2.16.3` mit fest gepinntem Image-Digest. Testdaten,
API-Secret und LiveKit-Konfiguration entstehen in einem temporären Verzeichnis
und werden beim Beenden entfernt. Das feste Secret ist ausschließlich für die
isolierte Testtopologie bestimmt.

## Phasen und Gates

| Phase | Automatisierter Nachweis |
|---|---|
| Control Plane | 202 Anmeldungen, 1.010 parallele Membership-Abfragen, Gerätebindung, ungültige Credentials, Session-Credential-Wiederverwendung und manipulierte Empfängerfelder |
| Group-Scope | zehn serverautorisierte Starts mit exakt 200 Empfängern |
| Unabhängige Scopes | acht gleichzeitige Team-Transmissionen sowie vier parallele LiveKit-Räume |
| Gleichzeitige Sprecher | zwei Audio-Publisher und 200 Subscriber, also 400 erwartete Track-Abonnements |
| Limits und Lifecycle | Sprecherlimit, Moderationsabbruch, 30-Sekunden-Timeout und Membership-Wechsel mit atomarem Abbruch parallel zu einem 202-Session-Soak |
| Netzstörung | zwei Publisher und 20 Subscriber bei 150 ms ausgehendem NetEm-Delay und 2 % ausgehendem Paketverlust |
| Gemeinsame Last | Control-Plane-Soak parallel zur 200-Subscriber-Medienlast und Container-Ressourcenmessung |
| Serverausfall | Control Plane für zwei Sekunden stoppen; laufende Sessions müssen die Unterbrechung erkennen und sich nach dem Neustart erholen |
| Native Sicherheit | Publikation ohne `canPublish`, Rechteentzug bei aktivem Track, Abo-Verbot und Cross-Room-Isolation |

Der Lauf schlägt fehl, wenn eine unerwartete Control-Plane-Antwort, eine falsche
Empfängerzahl, ein verlorenes Medienabonnement, ein Medienfehler oder eine
fehlende Wiederherstellung auftritt. Zusätzlich gelten:

- p95 der serverseitigen Transmission-Startautorisierung unter 300 ms,
- Membership-Propagation unter einer Sekunde,
- exakt null falsche Empfänger,
- exakt 200/200 Group-, 4 × 4/4 unabhängige, 400/400 gleichzeitige und
  40/40 NetEm-Abonnements.

`audio-start-latency.tsv` misst getrennt die Zeit vom Start des
Medienlastgenerators bis zum ersten aufgebauten entfernten
Audio-Track-Abonnement. Diese Definition schließt Prozessstart, Signalisierung
und Raumbeitritt bewusst ein und ist daher eine konservative
Ende-zu-Ende-Startmessung.

## Native Sicherheitsprobe

Während die Linux-Medienlast läuft, wird der native Windows-Quality-Gate-Client
gegen den in `external-livekit-endpoint.txt` ausgegebenen Endpunkt gestartet:

```powershell
.\scripts\build-windows.cmd -Preset windows-msvc-livekit-quality-gate

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\Invoke-LiveKitSecurityQualityGate.ps1 `
  -Url ws://192.0.2.10:17880 `
  -ApiKey hvc `
  -ApiSecret hvc-section-10-2-livekit-secret
```

Die Probe weist nach, dass ein Token ohne Publikationsrecht keinen
Mikrofontrack veröffentlichen kann, ein aktiver Track nach Rechteentzug
verschwindet, `canSubscribe=false` kein Audio-Abonnement erhält und keine
Teilnehmer oder Tracks eine Raumgrenze überschreiten.

## Ergebnisse und Artefakte

Ein vollständiger Referenzlauf auf Debian 13 mit einem ergänzenden nativen
Windows-Client bestand am 26. Juli 2026. Die Control Plane verarbeitete 1.435
Gate-Anfragen sowie 11.851 erfolgreiche parallele Hintergrundanfragen ohne
unerwarteten Fehler und ohne falschen Empfänger. Unter dieser Last lag die
p95-Startautorisierung bei 181,1 ms, die p95-Membership-Abfrage bei 245,0 ms
und die Membership-Propagation bei 217,0 ms. Die konservative
Audio-Startzeit betrug 445 ms für die 200-Subscriber-Phase und 1.531 ms für die
kombinierte Zwei-Sprecher-Phase. Der 30-Sekunden-Timeout, Moderation,
Sprecherlimits, der Membership-Abbruch und die Wiederherstellung nach
Serverausfall bestanden.

Unter gemeinsamer Last belegten Control Plane, LiveKit und Lastgenerator
zusammen weniger als 700 MiB. Der LiveKit-Prozess erreichte dabei rund 63 %
CPU, die Control Plane blieb unter 1 % CPU. Alle erwarteten Medienabonnements
wurden erreicht; die durch WebRTC nach NetEm verbleibende Paketverlustrate und
die gemeldete Fehlerrate waren null. Der native Rechteentzug entfernte den
aktiven Track nach 5 ms; alle übrigen nativen Sicherheitsproben meldeten
ebenfalls `PASS`.

Jeder Lauf schreibt unter `out/load-tests/<UTC-Zeit>`:

| Artefakt | Inhalt |
|---|---|
| `control-plane.json` | Gate-Ergebnisse und Latenzperzentile |
| `control-plane-gate-background.json` | parallele Last während der Lifecycle-Gates |
| `control-plane-combined.json` | paralleler Control-Plane-Soak |
| `control-plane-recovery.json` | Ausfall- und Wiederherstellungsnachweis |
| `audio-start-latency.tsv` | Audio-Startzeit je gemessener Medienphase |
| `media-*.log` | LiveKit-Summen, Bitraten, Paketverlust und Fehler |
| `resources.tsv` | CPU, Speicher, Netzwerk-I/O und Prozesszahlen |
| `services.log` | zusammengeführte Control-Plane- und LiveKit-Logs |

Echte Mikrofone, Wiedergabegeräte, Raw Input und subjektive Sprachqualität
bleiben Hardware-Gates. Sie ersetzen den reproduzierbaren 200-Spieler-Nachweis
nicht.
