# Server-Laufzeit

**Stand:** 27. Juli 2026

## Betriebsmodell

Die Control Plane läuft auf Debian 13 hinter einem TLS-terminierenden Reverse
Proxy. SQLite, LiveKit und der HTTP-Adapter bleiben getrennte Komponenten. Der
Prozess akzeptiert entweder den lokalen Ein-Spieler-Bootstrap oder den
Mehrbenutzermodus; beide Modi gleichzeitig werden abgelehnt.

Der produktive Start verwendet:

```text
--identity-file /run/secrets/hvc-identities
--database /var/lib/hvc/control-plane.db
--listen 0.0.0.0
--http-workers 8
--http-queue-capacity 64
--livekit-url wss://voice.example.invalid
--livekit-control-url ws://livekit:7880
--livekit-api-key hvc
--livekit-api-secret-file /run/secrets/livekit-api-secret
```

Die getrennte Lasttopologie, der 200-Spieler-Nachweis und die zugehörigen
Betriebsmetriken sind in
[load-and-security-tests.md](load-and-security-tests.md) beschrieben.

Die Identity-Datei ist tabulatorgetrennt. Jede nicht auskommentierte Zeile
enthält Bearer-Credential, Spieler-ID und eine kommaseparierte Liste erlaubter
Geräte-IDs. `*` erlaubt jedes Gerät. Produktionsmodus verlangt mindestens zwei
Accounts. Credentials werden nur in den Arbeitsspeicher gelesen und
zeitkonstant verglichen.

```text
opaque-token-1	player-1	desktop-1,laptop-1
opaque-token-2	player-2	*
```

Dieser Modus ist seit IAM-01 ausdrücklich ein befristeter Entwicklungs- und
Migrationspfad. IAM-02 ersetzt ihn im Produktbetrieb durch den persistenten
lokalen Accountadapter aus
[`identity-and-account-lifecycle.md`](identity-and-account-lifecycle.md).
Die Klartext-Bearer werden dabei nicht als Passwörter übernommen; die
Offline-Migration erhält Spieler-, Membership-, Rollen- und Auditbezüge und
erzeugt neue einmalige Aktivierungssecrets. Dateiadapter und Accountadapter
dürfen niemals gleichzeitig authentifizieren.

Memberships und Rollenrichtlinien liegen autoritativ in SQLite. Ein
`administrator` darf die vorhandenen administrativen Membership-Endpunkte
verwenden; `moderator` und `administrator` dürfen Transmissionen unterbrechen.
Diese Entscheidungen werden bei jeder Operation aus der aktuellen Membership
gelesen.

## Voice-Sicherheit

LiveKit-Raumtokens enthalten für alle Scopes zunächst `canPublish=false`.
Empfangsrechte bleiben davon unabhängig. Nach erfolgreicher atomarer
Transmission-Aktivierung ruft die Control Plane
`RoomService.UpdateParticipant` für genau den Scope-Raum und die
serverabgeleitete Spieleridentität auf.

Der Store entzieht das Recht bei regulärem PTT-Ende, Moderation, Timeout,
Session-Entfernung sowie Membership- oder Rechtewechsel. `canPublish=false`
entfernt über LiveKit zugleich eine aktive Publikation. Ein fehlgeschlagener
Start-Hook rollt die Aktivierung zurück und liefert
`voice_control_unavailable`.

Sprecherlimits stammen aus der autoritativen `ScopeDefinition`. Die Aktivierung
zählt unter demselben Store-Lock nur Transmissionen am exakt gleichen Team-,
Specialization- oder Group-Knoten. Ein volles Limit liefert
`speaker_limit_reached`.

## Laufzeitjobs

| Job | Intervall | Verhalten |
|---|---:|---|
| Transmission-Timeout | 250 ms | beendet überfällige Transmissionen und entzieht LiveKit-Rechte |
| Session-Bereinigung | 5 s | verarbeitet höchstens 256 abgelaufene Sessions |
| Audit-Aufbewahrung | 60 s | löscht höchstens 1.000 Events pro Lauf, die älter als 30 Tage sind |

Jobfehler beenden den Prozess nicht. Sie erzeugen ein strukturiertes
Fehlerereignis und werden in der nächsten Iteration erneut versucht.

## Membership-Propagation

Der Windows-Client fragt die eigene Membership alle 500 ms ab. Eine strikt
höhere Version beendet eine lokale Publikation ohne automatische Wiederaufnahme,
lädt passende Grants, ersetzt die Raumverbindungen und Empfangsabonnements und
aktualisiert die Membership-Anzeige. Damit liegt der normale Pfad unter dem
Ein-Sekunden-Ziel.

## HTTP, Readiness und Metriken

Der Linux-Listener verwendet einen festen Worker-Pool und eine begrenzte
Verbindungsqueue. Bei voller Queue antwortet er mit `503 server_overloaded`.
Der Überlastpfad verwirft die ungelesene Empfangsseite kontrolliert, bevor der
Socket geschlossen wird, damit Linux die HTTP-Antwort nicht durch einen
TCP-Reset ersetzt. Ein Linux-spezifischer CTest deckt diesen Pfad mit echten
Sockets ab.
`SIGINT` und `SIGTERM` stoppen die Annahme neuer Verbindungen; bereits
angenommene Requests werden abgearbeitet.

- `GET /api/v1/health` meldet Readiness nach erfolgreicher Migration und
  Adapterinitialisierung.
- `GET /api/v1/metrics` liefert Prometheus-Text für HTTP-Requests und -Fehler,
  aktive Transmissionen, LiveKit-Fehler und verworfene SQLite-Audit-Events.
- Betriebsereignisse werden als einzeilige JSON-Datensätze ausgegeben.
  Credentials und Empfänger-IDs werden nicht protokolliert.

## Docker und Compose

`Dockerfile` baut den Server auf Debian 13. `compose.yaml` startet Control
Plane, LiveKit `1.13.4` und Caddy `2.10.0` mit persistenten Volumes und lokaler
TLS-Terminierung für `localhost`. Bei einem extern erreichbaren Deployment
werden die beiden Caddy-Site-Adressen und `--livekit-url` auf den tatsächlichen
DNS-Namen umgestellt.

File-basierte Compose-Secrets behalten wegen der zugrunde liegenden
Bind-Mounts ihre Host-UID und -Rechte. Der Container-Launcher kopiert deshalb
nur die freigegebenen Secrets mit Modus `0400` in ein flüchtiges `tmpfs` und
startet die Control Plane anschließend dauerhaft als Benutzer `hvc`. Ein
HTTP-Healthcheck hält den Proxy zurück, bis Migration und Listener bereit sind.

Vor `docker compose up --build -d` werden die `.example`-Dateien unter
`deploy/secrets/` ohne Suffix kopiert, sichere Credentials eingesetzt und die
autoritative Membership-Datenbank bereitgestellt. Secrets und SQLite-Daten
werden nicht in Images aufgenommen.
