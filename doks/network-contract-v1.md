# Control-Plane-Netzwerkvertrag v1

**Stand:** 27. Juli 2026<br>
**Basis-Pfad:** `/api/v1`<br>
**Medientyp:** `application/json; charset=utf-8`

## Geltungsbereich

Dieser Vertrag bildet die bereits frameworkunabhängig implementierten
Session-, Membership- und Transmission-Anwendungsfälle auf HTTP ab. Die
Versionsnummer ist Bestandteil jedes Pfads und jeder JSON-Antwort. Inkompatible
Änderungen erhalten einen neuen Basis-Pfad; bestehende v1-Felder werden weder
umgedeutet noch entfernt.

Der erste Adapter unterstützt HTTP/1.1 unter Linux. Er ist für den Betrieb
hinter einem TLS-terminierenden Reverse Proxy vorgesehen. Ohne einen solchen
Proxy darf er nur an Loopback oder ein anderweitig geschütztes internes Netz
gebunden werden.

## Gemeinsame Regeln

- Der Linux-Listener akzeptiert höchstens 16 KiB Header und 64 KiB Body.
- `Transfer-Encoding` wird nicht unterstützt. Requests mit Body verwenden
  `Content-Length`.
- Jede Verbindung verarbeitet genau einen Request und wird danach geschlossen.
- Antworten enthalten `Cache-Control: no-store`, `X-HVC-API-Version: v1` und
  eine explizite `Content-Length`.
- Zustands- und benutzerbezogene Requests müssen `X-Correlation-ID` und
  `X-HVC-Device-ID` enthalten.
- IDs sind nicht leere, opake UTF-8-Zeichenketten. Clients dürfen aus ihrem
  Format keine Bedeutung ableiten.
- Zeitpunkte werden als Millisekunden seit der Unix-Epoche übertragen.
- Unbekannte JSON-Felder in Befehlen werden abgelehnt. Doppelte JSON-Felder,
  verschachtelte Werte und ungültige Zahlen werden ebenfalls abgelehnt.
- Interne Empfänger-IDs werden niemals ausgegeben. Eine erfolgreiche
  Startantwort enthält ausschließlich `recipient_count`.

Fehler verwenden ein einheitliches Envelope:

```json
{
  "api_version": "v1",
  "error": {
    "code": "machine_readable_code",
    "message": "Human-readable summary."
  }
}
```

## Authentifizierungsgrenze

Externe Credentials und interne Sessions sind absichtlich getrennt:

1. Nur `POST /api/v1/sessions` akzeptiert
   `Authorization: Bearer <external-credential>`.
2. Der HTTP-Adapter reicht das Credential ausschließlich als
   `AuthenticateSessionCommand` an `ISessionAuthenticator` weiter.
3. Nach erfolgreicher Prüfung wird die ausgestellte, gerätegebundene Session
   persistiert. Das Credential erscheint weder in Antworten noch in
   Transmission-Befehlen.
4. Alle geschützten v1-Endpunkte verlangen anschließend
   `Authorization: Session <session-id>`.
5. Ein Bearer-Credential wird an einem Session-geschützten Endpunkt abgelehnt.
   Session-Ablauf und Gerätebindung werden vor dem Anwendungsfall erneut
   geprüft.

Der Account-/Identity-Adapter ist hinter `IIdentityProvider` austauschbar. Der
zunächst angebundene Bootstrap-Provider liest genau ein Credential aus einer
Datei und ordnet es einem konfigurierten Spieler zu. Er dient nur der lokalen
Linux-Integration; ein produktiver Deployment-Adapter übernimmt
Credentialprüfung, Accountstatus, Geräterichtlinie und vorgelagerte Rate Limits.
Das Credential wird bewusst nicht als Kommandozeilenargument angenommen.

IAM-01 hat für den persistenten lokalen Accountdienst Passwortanmeldung,
Aktivierung, Erneuerung, Credentialwechsel sowie Geräte- und
Sessionwiderruf entschieden. Diese noch nicht implementierten Abläufe werden
in API-01 als neuer, versionierter Vertrag festgelegt. Insbesondere wird das
bestehende opake Bearer-Credential dieses Endpunkts weder als Passwort
umgedeutet noch um neue Pflichtfelder erweitert. Der vollständige
Lebenszyklus- und Migrationsvertrag steht in
[`identity-and-account-lifecycle.md`](identity-and-account-lifecycle.md).

## Bereitschaft

### `GET /api/v1/health`

Der Endpunkt ist nicht authentifiziert und bestätigt, dass Initialisierung und
Datenbankmigration vor dem Start des Listeners erfolgreich waren.

Antwort `200 OK`:

```json
{"api_version":"v1","status":"ready"}
```

## Session erstellen

### `POST /api/v1/sessions`

Erforderliche Header:

```text
Authorization: Bearer <external-credential>
X-HVC-Device-ID: <device-id>
X-Correlation-ID: <correlation-id>
```

Ein Request-Body ist nicht zulässig.

Antwort `201 Created`:

```json
{
  "api_version": "v1",
  "session_id": "ses_...",
  "player_id": "player-42",
  "device_id": "desktop-1",
  "expires_at_unix_ms": 1784995200000
}
```

Relevante Fehler sind `invalid_credentials`, `device_not_allowed`,
`account_disabled`, `rate_limited` und `invalid_authenticator_result`.

## Eigene Membership lesen

### `GET /api/v1/membership`

Erforderliche Header:

```text
Authorization: Session <session-id>
X-HVC-Device-ID: <device-id>
X-Correlation-ID: <correlation-id>
```

Die Antwort enthält nur die Membership des authentifizierten Spielers. Andere
Spieler desselben Snapshots werden nicht offengelegt.

Antwort `200 OK`:

```json
{
  "api_version": "v1",
  "membership_version": 42,
  "hierarchy_id": "hierarchy-1",
  "player_id": "player-42",
  "group_id": "group-1",
  "specialization_id": "specialization-1",
  "team_id": "team-1",
  "role_ids": ["speaker"],
  "connected": true,
  "can_receive_voice": true,
  "transmit_muted": false
}
```

`404 membership_unavailable` bedeutet, dass noch kein autoritativer Kontext für
den Spieler vorliegt. Administrative Membership-Updates werden erst mit dem
späteren, gesondert autorisierten Verwaltungsvertrag nach außen freigegeben;
die bestehende interne Compare-and-Replace-Schnittstelle bleibt davon
unverändert.

## Administrative Memberships

### `GET /api/v1/admin/memberships/{player-id}`

Liest die Membership eines angegebenen Spielers. Der Endpunkt verwendet zwar
eine gültige, gerätegebundene Session, verlangt aber zusätzlich eine positive
Entscheidung von `IAdministrativeMembershipAuthorizer`. Eine normale Session
erhält dadurch keine impliziten Verwaltungsrechte.

### `DELETE /api/v1/admin/memberships/{player-id}`

Entfernt den autoritativen Membership-Kontext und beendet aktive
Transmissionen des Spielers atomar. Auch dieser Endpunkt wird separat über
`canRemove` autorisiert. Der Linux-Bootstrap-Start verweigert beide
administrativen Operationen standardmäßig, bis ein Deployment einen
autoritativen Admin-/Rollenadapter bereitstellt.

### `PUT /api/v1/admin/memberships/{player-id}`

Ersetzt die Membership des angegebenen Spielers innerhalb des vollständigen
autoritativen Snapshots. `membership_version` muss strikt größer als die
gespeicherte Version sein; Lesen, Vergleichen und Ersetzen erfolgen atomar über
den bestehenden Control-Plane-Store. Der Endpunkt verlangt zusätzlich
`canReplace` vom administrativen Authorizer.

```json
{
  "membership_version": 43,
  "group_id": "group-1",
  "specialization_id": "specialization-1",
  "team_id": "team-1",
  "role_ids": "speaker,moderator",
  "connected": true,
  "can_receive_voice": true,
  "transmit_muted": false
}
```

`role_ids` ist eine kommaseparierte Liste opaker Rollen-IDs. Ein erfolgreicher
Wechsel beendet eine aktive Transmission des Spielers atomar. Veraltete oder
wiederholte Versionen liefern `409 membership_version_not_newer`.

## Voice-Grant-Vorbereitung

`VoiceGrantAuthorizationService` leitet kurzlebige Claims ausschließlich aus der aktiven, gerätegebundenen
Session, der aktuellen Membership-Version und der serverseitigen Rollenrichtlinie
ab. Die Laufzeit ist konfigurierbar und wird zusätzlich durch das Session-Ende
begrenzt. Der `hvc-livekit`-Adapter signiert daraus pro autorisiertem Scope ein
kurzlebiges HS256-JWT. Team, Specialization und Group werden auf getrennte Räume
abgebildet. Ein Senderecht macht den Scope-Grant verbindbar, das initiale Token
enthält jedoch stets `canPublish=false`. Die Control Plane erteilt das
Publikationsrecht erst nach erfolgreicher atomarer Transmission-Aktivierung
über LiveKit RoomService. `canSubscribe` wird unabhängig davon nach dem
Least-Privilege-Prinzip gesetzt. Geräte-ID und Membership-Version stehen als
signierte Participant-Metadaten zur Verfügung.

### `POST /api/v1/voice-grants`

Der geschützte Endpunkt akzeptiert keinen Body. Er prüft Session,
Gerätebindung, Ablauf und die aktuelle autoritative Membership erneut. Nur
Scopes mit mindestens einem Sende- oder Empfangsrecht werden ausgegeben.

Antwort `201 Created`:

```json
{
  "api_version": "v1",
  "server_url": "wss://voice.example.invalid",
  "membership_version": 42,
  "expires_at_unix_ms": 1784994300000,
  "grants": [
    {
      "scope": "team",
      "room_name": "team:team-1",
      "access_token": "short-lived-jwt"
    },
    {
      "scope": "group",
      "room_name": "group:group-1",
      "access_token": "short-lived-jwt"
    }
  ]
}
```

`room_name` dient ausschließlich der Diagnose und Zuordnung; der Client leitet
daraus keine Berechtigung ab. Verbindlich sind Scope, Server-URL und das
signierte Zugriffstoken. Ein Client kann deshalb zwischen einem und drei
autorisierte Scope-Grants erhalten.

Die LiveKit-API-Credentials werden serverseitig konfiguriert und niemals an den
Client ausgegeben. Ohne konfigurierte Grant-Ausstellung antwortet der Endpunkt
mit `503 voice_grants_unavailable`.

## Transmission starten

### `POST /api/v1/transmissions`

Neben den gemeinsamen Session-Headern enthält der Body:

```json
{
  "client_transmission_id": "client-ptt-17",
  "scope": "group",
  "membership_version": 42
}
```

`scope` ist `team`, `specialization` oder `group`. Eine Sender-ID und eine
Empfängerliste sind nicht zulässig.

Antwort `201 Created`:

```json
{
  "api_version": "v1",
  "transmission_id": "tx_...",
  "client_transmission_id": "client-ptt-17",
  "scope": "group",
  "membership_version": 42,
  "recipient_count": 7
}
```

Autorisierungs- und Konfliktfehler verwenden dieselben stabilen Codes wie die
Anwendungsschicht, darunter `voice_scope_not_authorized`,
`voice_membership_stale`, `sender_already_transmitting`,
`speaker_limit_reached`, `voice_control_unavailable` und `rate_limited`.

## Transmission beenden

### `DELETE /api/v1/transmissions/{transmission-id}`

Der Body ist leer. Session und Gerät müssen der gestarteten Transmission
gehören.

Antwort `200 OK`:

```json
{
  "api_version": "v1",
  "transmission_id": "tx_...",
  "status": "ended",
  "stop_reason": "push_to_talk_released"
}
```

## Moderationsabbruch

### `POST /api/v1/transmissions/{transmission-id}/interrupt`

Der Body ist leer. Die Anwendungsschicht prüft die Moderationsberechtigung
unabhängig vom HTTP-Adapter. Der initiale Linux-Entry-Point verweigert
Moderationsabbrüche, bis ein autoritativer Rollenadapter angebunden ist.

Antwort `200 OK`:

```json
{
  "api_version": "v1",
  "transmission_id": "tx_...",
  "status": "ended",
  "stop_reason": "moderation_interrupted"
}
```

## Linux-Start

Die Datenbankmigration läuft vor dem Listener. Ein Credential mit restriktiven
Dateirechten wird ohne abschließenden Zeilenumbruch gelesen:

```bash
hvc-control-plane \
  --database /var/lib/hvc/control-plane.db \
  --listen 127.0.0.1 \
  --port 8080 \
  --bootstrap-token-file /run/secrets/hvc-bootstrap-token \
  --bootstrap-player player-42 \
  --livekit-url ws://127.0.0.1:7880 \
  --livekit-api-key devkey \
  --livekit-api-secret-file /run/secrets/livekit-api-secret
```

Eine gestartete Transmission bleibt absichtlich flüchtig. Nach einem
Prozessneustart wird sie nicht wiederhergestellt.

## Betriebsmetriken

### `GET /api/v1/metrics`

Der nicht authentifizierte, nur im internen Betriebsnetz bereitzustellende
Endpunkt liefert Prometheus-Text für HTTP-Requests und -Fehler, aktive
Transmissionen, LiveKit-Control-Fehler und verworfene Audit-Events.
