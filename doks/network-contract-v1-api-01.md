# Control-Plane-Netzwerkvertrag v1 – Directory, Identity und Verwaltung

**Arbeitspaket:** API-01  
**Stand:** 27. Juli 2026  
**Status:** Verbindlicher Vertrag; Directory und Presence sind mit DIR-01
implementiert, die übrige Laufzeitimplementierung folgt in IAM-02, IAM-03,
ADM-01 und MOD-01
**Basis-Pfad:** `/api/v1`  
**Medientyp:** `application/json; charset=utf-8`

## Zweck und Geltung

Dieses Dokument erweitert den bestehenden
[Control-Plane-Netzwerkvertrag v1](network-contract-v1.md) additiv um:

- das datenschutzbegrenzte Verzeichnis und den getrennten Presence-Zustand;
- Passwortanmeldung, Aktivierung, Erneuerung und erneute Authentifizierung;
- die eigene Account-, Profil-, Geräte- und Sessionverwaltung;
- Account-, Einladungs-, Hierarchie-, Membership- und Rollenverwaltung;
- effektive Rechte und eine Vorschau geplanter Rechteänderungen;
- die Liste aktiver Transmissionen für die Moderation;
- administrative Auditdatensätze.

Die gemeinsamen HTTP-, JSON-, Header-, Größen-, TLS- und Fehlerregeln aus
`network-contract-v1.md` gelten unverändert. Dieses Dokument beschreibt den
vor der Implementierung eingefrorenen Zielvertrag. Ein hier festgelegter
Endpunkt darf erst als verfügbar dokumentiert werden, wenn sein späteres
Arbeitspaket die zugehörigen Positiv-, Negativ-, Autorisierungs- und
Datenschutztests bestanden hat.

## Kompatibilitäts- und Migrationsgrenze

Die bereits implementierten Voice-Verträge bleiben unverändert:

- `POST /api/v1/sessions` akzeptiert weiterhin ausschließlich ein opakes
  `Authorization: Bearer`-Credential und keinen Body.
- Das Bearer-Credential wird weder als Passwort noch als Aktivierungs- oder
  Erneuerungssecret interpretiert.
- `GET /api/v1/membership`, Voice-Grants, Transmissionen und die vorhandenen
  Endpunkte unter `/api/v1/admin/memberships/{player-id}` behalten ihre
  bisherigen Felder und Fehlercodes.
- Das bisherige `role_ids`-Stringfeld des administrativen Membership-`PUT`
  wird nicht in ein Array umgedeutet. Der neue, kanonische Vertrag verwendet
  deshalb einen anderen Ressourcenpfad.

Im dateibasierten Entwicklungs- und Migrationsmodus bleibt nur der bestehende
Session-Endpunkt aktiv. Die neuen `/auth`-, `/self`-, `/directory`-,
`/moderation`- und erweiterten `/admin`-Ressourcen antworten dort mit
`503 persistent_identity_unavailable`, sofern sie persistente Accountdaten
benötigen.

Nach der Offline-Migration startet der Dienst ausschließlich im persistenten
Accountmodus:

1. Neue Clients verwenden `/auth/login`, `/auth/activate` und `/auth/refresh`.
2. Bereits ausgestellte Legacy-Sessions dürfen höchstens bis zu ihrem
   bisherigen Ablauf weiterverwendet werden.
3. `POST /api/v1/sessions` antwortet im persistenten Accountmodus mit
   `410 legacy_identity_mode_disabled`.
4. Ein Downgrade zum Identity-Dateimodus ist nach Accountänderungen
   unzulässig.

Damit ist kein bestehendes v1-Feld still umgedeutet und ein Client kann den
Servermodus ohne Ausprobieren von Passwortwerten erkennen.

## Gemeinsame Sicherheitsregeln

### Authentifizierung

Es gibt vier klar getrennte Grenzen:

| Grenze | Authentifizierung |
|---|---|
| Öffentliche Authentifizierung | kein `Authorization`-Header; Secret nur im JSON-Body |
| Geschützte Benutzerressource | `Authorization: Session <access-session>` |
| Moderation | gültige Session plus aktuelle serverseitige Rolle `moderator` oder `administrator` |
| Administration | gültige Session plus aktuelle serverseitige Rolle `administrator` |

Rollen in einer Session oder in einer Clientansicht sind keine Autorität.
Jeder Moderations- und Verwaltungsrequest lädt die aktuelle Account-, Session-,
Membership- und Rollenzuweisung erneut. Deaktivierung, Widerruf oder
Rollenverlust wirken deshalb auf den nächsten Request.

Öffentliche Authentifizierungsendpunkte verlangen
`X-HVC-Device-ID` und `X-Correlation-ID`, aber keinen
`Authorization`-Header. Ein unerwarteter Header wird mit
`400 unexpected_authorization` abgelehnt, damit Secrets nicht versehentlich
über zwei Grenzen gesendet werden.

### Generische öffentliche Fehler

Login, Aktivierung und Erneuerung verwenden für alle folgenden Ursachen
dieselbe Antwort mit demselben Status und derselben öffentlichen Form:

```http
HTTP/1.1 401 Unauthorized
```

```json
{
  "api_version": "v1",
  "error": {
    "code": "authentication_failed",
    "message": "Authentication could not be completed."
  }
}
```

Dies gilt insbesondere für unbekannten Anmeldenamen, falsches Passwort,
deaktivierten Account, fehlendes Credential, ungültige, abgelaufene,
verbrauchte oder widerrufene Einladung sowie unbekanntes, abgelaufenes,
verbrauchtes oder widerrufenes Erneuerungssecret. Antwortstatus, Feldmenge und
öffentliche Fehlerkategorie dürfen diese Ursachen nicht unterscheiden.

Rate Limits antworten einheitlich mit `429 rate_limited` und einem ganzzahligen
`Retry-After`-Header. Ein Rate Limit darf weder Account-ID noch
Accountzustand ausgeben. Eingaben außerhalb der öffentlichen Syntaxgrenzen
antworten vor der Accountsuche mit `400 invalid_request`.

### Geheimnisse

Passwort, Einladungssecret, Erneuerungssecret und
Reauthentifizierungsnachweis:

- stehen ausschließlich in einem JSON-Body oder in der erfolgreichen,
  einmaligen Ausstellungsantwort;
- sind niemals Pfad-, Query-, Cookie- oder URL-Bestandteil;
- werden niemals in Fehlern, Audit, Betriebslogs oder Diagnoseartefakten
  wiederholt;
- werden vom HTTP-Adapter vor Body-Logging und Exceptiontexten redigiert;
- haben pro Feld höchstens 1.024 UTF-8-Bytes; Passwörter unterliegen zusätzlich
  den Regeln aus `identity-and-account-lifecycle.md`.

Servererzeugte Zugriffs-, Einladungs-, Erneuerungs- und
Reauthentifizierungssecrets besitzen mindestens 256 Bit CSPRNG-Entropie. Ihre
Clientwerte sind opak, bedeutungslos und enthalten weder Account- noch
Personendaten. Persistiert werden ausschließlich serverseitig zuordenbare
kryptografische Hashes.

Antworten mit neuem Einladungs-, Erneuerungs- oder
Reauthentifizierungssecret enthalten zusätzlich
`Pragma: no-cache`; `Cache-Control: no-store` gilt ohnehin für jede v1-Antwort.

### Ressourcenlimits

- Anzeigenamen und Gerätenamen: 1 bis 128 Unicode-Codepoints, gültiges UTF-8.
- Gründe administrativer Änderungen: 1 bis 512 Unicode-Codepoints.
- Suchtexte: höchstens 128 Unicode-Codepoints.
- Rollen- und Hierarchiebezeichner: nicht leere opake IDs mit höchstens
  128 UTF-8-Bytes.
- Listen enthalten keine doppelten IDs und sind stabil nach opaker ID
  aufsteigend sortiert, sofern der Endpunkt keine andere Sortierung festlegt.
- Unbekannte oder doppelte JSON-Felder werden wie im bestehenden Vertrag
  abgelehnt.
- Unbekannte, doppelte oder für den Endpunkt nicht erlaubte Queryparameter
  werden mit `400 invalid_query` abgelehnt.

## Versionen, Konflikte und Paging

### Ressourcen-Versionen

Account, Profil, Einladung, Gerät, Hierarchie, Membership, Rollenkatalog und
Rollenrichtlinie besitzen voneinander getrennte, strikt steigende positive
64-Bit-Versionen. Ein `GET` einer einzeln adressierten Ressource liefert:

```http
ETag: "account-17"
```

Jede ändernde Operation auf einer vorhandenen Ressource verlangt den exakt
zuvor gelesenen starken `If-Match`-Wert.

- Fehlender Header: `428 precondition_required`.
- Syntaktisch ungültiger Header: `400 invalid_precondition`.
- Abweichende Version: `412 version_conflict` mit dem aktuellen `ETag`, aber
  ohne den aktuellen Ressourceninhalt.
- Erfolgreiche Änderung: neuer `ETag` und die neue ganzzahlige Version im
  Antwortobjekt.

`If-Match: *` ist unzulässig. Create-Operationen verwenden keinen
`If-Match`-Header. Die bestehenden Membership-Adminendpunkte behalten aus
Kompatibilitätsgründen ihre bisherige
`membership_version_not_newer`-Semantik.

### Cursor-Paging

Listenparameter:

| Parameter | Regel |
|---|---|
| `limit` | optional, Standard 50, Minimum 1, Maximum 200 |
| `cursor` | optionaler opaker, URL-sicherer Cursor aus der vorherigen Antwort |

Eine Listenantwort besitzt immer:

```json
{
  "api_version": "v1",
  "snapshot_version": 91,
  "items": [],
  "next_cursor": null
}
```

Der Server bindet den Cursor kryptografisch an Akteur, Autorisierungsbereich,
Filter, Sortierung und `snapshot_version`. Ein Client darf ihn nicht
interpretieren oder verändern.

- Syntaktisch ungültig, manipuliert oder für andere Filter verwendet:
  `400 invalid_cursor`.
- Für einen anderen Akteur oder Autorisierungsbereich ausgestellt:
  `403 cursor_forbidden`.
- Snapshot während des Durchblätterns nicht mehr verfügbar:
  `409 page_snapshot_changed`; der Client beginnt ohne Cursor neu.
- Eine leere letzte Seite enthält `next_cursor: null`.

Secrets und personenbezogene Filterwerte werden nicht in lesbarer Form in
einem Cursor gespeichert. Directory-Presence verwendet wegen seiner hohen
Änderungsrate keinen Paging-Cursor, sondern den gesonderten Delta-Vertrag.

## Öffentliche Datenmodelle

### Directory-Knoten

```json
{
  "node_id": "team-red-1",
  "node_type": "team",
  "parent_node_id": "specialization-red",
  "display_name": "Red Team 1",
  "sort_index": 10
}
```

`node_type` ist `group`, `specialization` oder `team`. Genau ein Group-Knoten
ist die Wurzel der sichtbaren Antwort. Eine Specialization verweist auf diese
Group, ein Team auf eine Specialization derselben Group.

### Sichtbarer Teilnehmer

```json
{
  "player_id": "player-42",
  "display_name": "Alex",
  "primary_team_id": "team-red-1",
  "public_role_ids": ["team-leader"]
}
```

Ein sichtbarer Teilnehmer enthält niemals `account_id`, Anmeldename,
Credential-, Geräte-, Session-, IP-, Audit- oder interne
Berechtigungsmetadaten. `public_role_ids` enthält nur ausdrücklich als
öffentlich markierte Rollen. Das Fehlen einer Rolle beweist nicht, dass der
Teilnehmer sie nicht besitzt.

### Öffentliche Rolle

```json
{
  "role_id": "team-leader",
  "display_name": "Team leader"
}
```

Interne Rollenrichtlinien und effektive Rechte sind kein Bestandteil des
Directory.

## Directory und Presence

### `GET /api/v1/directory`

Die Control Plane leitet die sichtbare Group ausschließlich aus der aktuellen
Membership der authentifizierten Session ab. Es gibt absichtlich keinen
Group-ID-Queryparameter und keinen Endpunkt zum Auflisten fremder Groups.

Antwort `200 OK`:

```json
{
  "api_version": "v1",
  "directory_version": 44,
  "group_id": "group-alpha",
  "nodes": [
    {
      "node_id": "group-alpha",
      "node_type": "group",
      "parent_node_id": null,
      "display_name": "Group Alpha",
      "sort_index": 0
    }
  ],
  "public_roles": [],
  "participants": [
    {
      "player_id": "player-42",
      "display_name": "Alex",
      "primary_team_id": "team-red-1",
      "public_role_ids": []
    }
  ]
}
```

Die Antwort enthält alle für den authentifizierten Spieler sichtbaren Knoten
und höchstens 200 sichtbare Teilnehmer. Eine größere Group liefert
`413 directory_limit_exceeded` und wird nicht still abgeschnitten. Eine
fehlende oder inaktive eigene Membership liefert
`404 directory_unavailable`.

`directory_version` steigt strikt bei sichtbarer Hierarchie-, Profil-,
Membership- oder öffentlicher Rollenänderung. Ein Request mit
`If-None-Match: "directory-44"` erhält `304 Not Modified`, wenn seine aktuelle
autorisierte Sicht unverändert ist. Ein `304` enthält keinen Body.

### `GET /api/v1/directory/presence`

Presence ist von Membership, Audioverfügbarkeit und Sprechen getrennt. Ohne
Queryparameter liefert der Endpunkt einen vollständigen Snapshot:

```json
{
  "api_version": "v1",
  "presence_version": 108,
  "mode": "snapshot",
  "observed_at_unix_ms": 1785175200000,
  "entries": [
    {
      "player_id": "player-42",
      "state": "online"
    }
  ]
}
```

`state` ist `online` oder `offline`. Mehrere gleichzeitige
Transportverbindungen desselben Spielers werden zu genau einem Zustand
zusammengeführt. Geräteanzahl, Scope-Verbindungen, IP-Adresse, letzte
Anmeldezeit und historischer Last-Seen-Zeitpunkt werden nicht ausgegeben.

Mit `?after_version=108` liefert der Endpunkt alle seit dieser exklusiven
Version sichtbaren Änderungen, pro Spieler höchstens den neuesten Zustand:

```json
{
  "api_version": "v1",
  "presence_version": 111,
  "mode": "delta",
  "observed_at_unix_ms": 1785175200500,
  "entries": [
    {
      "player_id": "player-17",
      "state": "offline"
    }
  ]
}
```

Änderungen einer fremden Group werden weder in der Version noch in den
Einträgen der sichtbaren Antwort offengelegt. Ist die angefragte Version null,
zukünftig oder nicht mehr im begrenzten Deltafenster verfügbar, antwortet der
Server mit `409 presence_snapshot_required`. Der Client lädt anschließend
einen vollständigen Snapshot. Polling schneller als der serverseitig
angegebene `Retry-After`-Wert kann mit `429 rate_limited` abgelehnt werden.

## Authentifizierung

### `POST /api/v1/auth/login`

```json
{
  "login_name": "alex",
  "password": "correct horse battery staple",
  "device_name": "Gaming desktop"
}
```

Erfolg `201 Created`:

```json
{
  "api_version": "v1",
  "session_id": "ses_...",
  "session_expires_at_unix_ms": 1785176100000,
  "refresh_secret": "rfr_...",
  "refresh_expires_at_unix_ms": 1792951200000,
  "account_id": "account-42",
  "player_id": "player-42",
  "device_id": "device-7",
  "account_version": 17,
  "requires_password_change": false
}
```

Der Server kanonisiert nur den Anmeldenamen gemäß IAM-01. Das Passwort wird
bytegenau als gültiges UTF-8 geprüft, weder getrimmt noch normalisiert.
Erfolgreiche Anmeldung erzeugt oder aktualisiert das serverseitige Gerät zur
Installation aus `X-HVC-Device-ID`.

### `POST /api/v1/auth/activate`

```json
{
  "login_name": "alex",
  "invitation_secret": "inv_...",
  "new_password": "a new long password",
  "device_name": "Gaming desktop"
}
```

Aktivierung verbraucht Einladung, setzt Credential, aktiviert Account und
stellt dieselbe Antwortform wie Login atomar aus. Passwortpolicy-Verletzungen,
die ohne Accountsuche feststehen, liefern `422 password_policy_violation` mit
einem nicht geheimen `error.message`. Alle einladungs- oder
accountabhängigen Ablehnungen verwenden dagegen `authentication_failed`.

### `POST /api/v1/auth/refresh`

```json
{
  "refresh_secret": "rfr_..."
}
```

Erfolg `201 Created` verwendet die Login-Antwortform mit neuer
`session_id` und neuem `refresh_secret`. Das alte Erneuerungssecret wird in
derselben Transaktion verbraucht. Wiederverwendung widerruft die gesamte
Tokenfamilie und liefert öffentlich nur `authentication_failed`.

## Eigene Accountressourcen

### `GET /api/v1/self/account`

Antwort `200 OK` mit `ETag: "account-17"`:

```json
{
  "api_version": "v1",
  "account_id": "account-42",
  "account_status": "active",
  "account_version": 17,
  "login_name": "alex",
  "player_id": "player-42",
  "profile": {
    "display_name": "Alex",
    "profile_version": 4
  }
}
```

### `PUT /api/v1/self/profile`

Der `If-Match`-Header bezieht sich auf den ETag
`"profile-<profile_version>"`, den `GET /self/account` zusätzlich als
`X-HVC-Profile-ETag` liefert.

```json
{
  "display_name": "Alex Example"
}
```

Erfolg `200 OK` gibt das Profil mit neuer Version und `ETag` zurück.

### `PUT /api/v1/self/password`

```json
{
  "current_password": "old long password",
  "new_password": "new long password",
  "keep_current_session": true
}
```

Der Request verlangt `If-Match: "account-17"`. Erfolg `200 OK` enthält
`account_version`, den neuen `ETag`, `other_sessions_revoked: true` und bei
`keep_current_session: true` eine neue `session_id` mit Ablauf. Die bisherige
aktuelle Session wird in jedem Fall ungültig; der neue Wert verhindert die
Weiterverwendung einer vor dem Credentialwechsel bekannten Session. Die
Antwort enthält kein Erneuerungssecret. Falsches aktuelles Passwort liefert
`401 reauthentication_failed` in generischer Form. Der neue Wert unterliegt
der Passwortpolicy. Bei `keep_current_session: false` wird keine Ersatzsession
ausgestellt.

### `POST /api/v1/auth/reauthentication-proofs`

```json
{
  "current_password": "current long password"
}
```

Erfolg `201 Created`:

```json
{
  "api_version": "v1",
  "reauthentication_proof": "reauth_...",
  "expires_at_unix_ms": 1785175500000
}
```

Der Nachweis ist an Account, aktuelle Session und Gerät gebunden, höchstens
fünf Minuten gültig und genau einmal verwendbar. Sensible administrative
Operationen übertragen ihn im Header
`X-HVC-Reauthentication: <proof>`. Ein fehlender, falscher oder verbrauchter
Nachweis liefert einheitlich `401 reauthentication_required`.

### `DELETE /api/v1/self/session`

Beendet die aktuelle Zugriffssession und ihre Erneuerungsfamilie. Erfolg ist
`204 No Content`. Eine Wiederholung mit bereits ungültiger Session erhält die
normale generische Session-Ablehnung.

### `GET /api/v1/self/devices`

Die paginierte Antwort verwendet folgende Einträge:

```json
{
  "device_id": "device-7",
  "device_name": "Gaming desktop",
  "first_seen_at_unix_ms": 1784000000000,
  "last_seen_at_unix_ms": 1785175000000,
  "revoked": false,
  "device_version": 3
}
```

### `DELETE /api/v1/self/devices/{device-id}`

Verlangt den aus `device_version` gebildeten ETag
`"device-<device_version>"` in `If-Match`. Die aktuelle Geräte-ID darf nur
widerrufen werden, wenn `?confirm_current=true` gesetzt ist. Erfolg widerruft
Gerät, Sessions und Erneuerungsfamilien atomar und antwortet `204 No Content`.

### `GET /api/v1/self/sessions`

Die paginierte Antwort enthält niemals das verwendbare `session_id` oder
Erneuerungssecret:

```json
{
  "session_record_id": "session-record-9",
  "device_id": "device-7",
  "created_at_unix_ms": 1785170000000,
  "last_refreshed_at_unix_ms": 1785175000000,
  "expires_at_unix_ms": 1785176100000,
  "current": true,
  "session_version": 2
}
```

### `DELETE /api/v1/self/sessions/{session-record-id}`

Verlangt den aus `session_version` gebildeten ETag
`"session-<session_version>"` in `If-Match`. Für die aktuelle Session gelten
dieselben Bestätigungsregeln wie beim aktuellen Gerät. Erfolg widerruft die
zugehörige Erneuerungsfamilie und antwortet `204 No Content`.

### `POST /api/v1/self/sessions/revoke-others`

Der Endpunkt verlangt einen gültigen Reauthentifizierungsnachweis und einen
leeren Body. Er widerruft alle anderen Sessions und Erneuerungsfamilien des
Accounts atomar, lässt die aktuelle Session bestehen und antwortet:

```json
{
  "api_version": "v1",
  "revoked_session_count": 3
}
```

## Administration

Alle Endpunkte dieses Abschnitts verlangen die aktuelle globale Rolle
`administrator`. Eine nur gruppenbezogene oder öffentlich sichtbare Rolle mit
gleichem Anzeigenamen reicht nicht aus.

### Accountliste und Account

`GET /api/v1/admin/accounts` ist paginiert und unterstützt ausschließlich:

- `query`: Präfixsuche auf kanonischem Anmeldenamen oder Anzeigenamen;
- `status`: `pending_activation`, `active` oder `disabled`;
- die gemeinsamen `limit`- und `cursor`-Parameter.

Ein Listeneintrag enthält `account_id`, `login_name`, `account_status`,
`account_version`, `player_id`, `display_name` und `profile_version`.
Credential-, Hash-, Secret-, Session- und Gerätewerte fehlen.

`GET /api/v1/admin/accounts/{account-id}` ergänzt Erstellungs- und
Änderungszeitpunkte, aber weiterhin kein Credentialmaterial. Die Antwort
liefert den Account-ETag.

`PUT /api/v1/admin/accounts/{account-id}/profile` ändert den öffentlichen
Anzeigenamen unter dem Profil-ETag. Erfolg gibt Profilversion, Profil-ETag und
Audit-ID zurück.

### `POST /api/v1/admin/accounts`

```json
{
  "login_name": "alex",
  "player_id": "player-42",
  "display_name": "Alex"
}
```

Erfolg `201 Created`:

```json
{
  "api_version": "v1",
  "account_id": "account-42",
  "account_status": "pending_activation",
  "account_version": 1,
  "player_id": "player-42",
  "profile_version": 1,
  "invitation_id": "invitation-8",
  "invitation_version": 1,
  "invitation_secret": "inv_...",
  "invitation_expires_at_unix_ms": 1785261600000,
  "audit_event_id": "audit-101"
}
```

Das Secret wird genau in dieser erfolgreichen Antwort ausgegeben. Bei
verlorener Antwort stellt der Administrator über den Einladungsendpunkt eine
neue Einladung aus; ältere offene Einladungen desselben Zwecks werden
widerrufen. Doppelte Anmeldenamen oder Player-IDs liefern
`409 account_identity_conflict`, ohne bestehende Accountdetails auszugeben.

### `POST /api/v1/admin/accounts/{account-id}/invitations`

Verlangt den Account-ETag in `If-Match`.

```json
{
  "purpose": "activation",
  "reason": "Replacement after the original delivery failed"
}
```

`purpose` ist `activation`, `credential_reset` oder `recovery`; `recovery`
bleibt dem Offline-Verfahren vorbehalten und wird online mit
`403 recovery_offline_only` abgelehnt. Die Antwort enthält ID, Version,
einmaliges Secret, Ablauf und Audit-ID.

`GET /api/v1/admin/accounts/{account-id}/invitations` listet paginiert
`invitation_id`, `purpose`, `status`, Ausgabe-, Ablauf- und Verbrauchszeit,
`invitation_version`, aber niemals Secret oder Secret-Hash.

`DELETE /api/v1/admin/accounts/{account-id}/invitations/{invitation-id}`
verlangt den aus `invitation_version` gebildeten ETag, einen nicht leeren
`reason`-Body und widerruft nur eine noch offene Einladung. Erfolg enthält
neue Version, `status: "revoked"` und Audit-ID.

### Accountzustandsaktionen

| Endpunkt | Zusätzliche Regel |
|---|---|
| `POST /api/v1/admin/accounts/{id}/disable` | Account-ETag, nicht leerer Grund und Reauthentifizierungsnachweis |
| `POST /api/v1/admin/accounts/{id}/enable` | Account-ETag und nicht leerer Grund |
| `POST /api/v1/admin/accounts/{id}/reset-credential` | Account-ETag, nicht leerer Grund und Reauthentifizierungsnachweis |

Jeder Body besitzt nur:

```json
{
  "reason": "Administrative request"
}
```

Erfolg `200 OK` enthält den neuen Accountzustand, `account_version`,
`audit_event_id` und beim Reset zusätzlich die einmalige Reset-Einladung.
Deaktivierung und Reset widerrufen Credential, Sessions,
Erneuerungsfamilien und offene Einladungen atomar. Membership, Rollen und
Auditgeschichte bleiben erhalten.

Administratoren verwenden zum Auflisten und Widerrufen fremder Geräte und
Sessions dieselben Modelle unter:

- `GET /api/v1/admin/accounts/{account-id}/devices`
- `DELETE /api/v1/admin/accounts/{account-id}/devices/{device-id}`
- `GET /api/v1/admin/accounts/{account-id}/sessions`
- `DELETE /api/v1/admin/accounts/{account-id}/sessions/{session-record-id}`

Jeder erfolgreiche Widerruf liefert eine `audit_event_id` in einer
`200 OK`-Antwort; fremde Ressourcen verwenden deshalb nicht `204`.

### Hierarchiekatalog

`GET /api/v1/admin/hierarchies` liefert paginiert `hierarchy_id`,
`display_name` und `hierarchy_version`.

`POST /api/v1/admin/hierarchies` erzeugt aus `hierarchy_id`, `display_name`
und `nodes` einen neuen, vollständig validierten Katalog. Erfolg ist
`201 Created` mit Version eins, ETag und Audit-ID. Eine vorhandene ID liefert
`409 hierarchy_identity_conflict`.

`GET /api/v1/admin/hierarchies/{hierarchy-id}` liefert den vollständigen
Katalog ohne Teilnehmer:

```json
{
  "api_version": "v1",
  "hierarchy_id": "hierarchy-main",
  "display_name": "Main hierarchy",
  "hierarchy_version": 12,
  "nodes": []
}
```

`PUT /api/v1/admin/hierarchies/{hierarchy-id}` ersetzt den vollständigen
Katalog unter dem Hierarchie-ETag. Der Body enthält `display_name` und
`nodes` in der Form des Directory-Knotens. Der Server prüft, dass jeder
Group-Knoten eine Wurzel ist, jede Specialization zu genau einer Group und
jedes Team zu genau einer Specialization derselben Group gehört; Elternbezüge
sind azyklisch und IDs eindeutig. Das Entfernen referenzierter Knoten liefert
`409 hierarchy_in_use`; Memberships werden niemals still gelöscht oder
verschoben. Erfolg gibt neuen ETag, Version und Audit-ID zurück.

### Kanonische Membership-Ressource

`GET /api/v1/admin/players/{player-id}/membership` liefert:

```json
{
  "api_version": "v1",
  "player_id": "player-42",
  "hierarchy_id": "hierarchy-main",
  "group_id": "group-alpha",
  "specialization_id": "specialization-red",
  "team_id": "team-red-1",
  "role_assignments": [
    {
      "role_id": "team-leader",
      "scope_node_id": "team-red-1"
    }
  ],
  "connected": true,
  "can_receive_voice": true,
  "transmit_muted": false,
  "membership_version": 44
}
```

`PUT /api/v1/admin/players/{player-id}/membership` ersetzt diese Felder ohne
`player_id` und `membership_version`; der Membership-ETag ist verpflichtend.
Der Server prüft Hierarchiebezüge und Rollen-Scope. Erfolg beendet bei einer
wirksamen Membership- oder Rechteänderung aktive Transmissionen atomar und
liefert neuen ETag, Version und Audit-ID.

Ändert sich dadurch eine effektive Moderations- oder Verwaltungsberechtigung,
werden alle Zugriffs- und Erneuerungssessions des betroffenen Accounts
widerrufen. Die nächste Anmeldung erhält eine neue, nicht fixierbare
Sessionkennung und die neuen Rechte ausschließlich aus dem dann aktuellen
serverseitigen Kontext.

`DELETE /api/v1/admin/players/{player-id}/membership` verlangt ETag und
nicht leeren `reason`-Body, beendet aktive Transmissionen, entfernt aber weder
Account noch Profil oder Rollen-Auditgeschichte.

### Rollenkatalog und Richtlinien

`GET /api/v1/admin/roles` ist paginiert. Ein Eintrag lautet:

```json
{
  "role_id": "team-leader",
  "display_name": "Team leader",
  "public": true,
  "role_version": 8,
  "permissions": {
    "team_transmit": true,
    "specialization_transmit": true,
    "group_transmit": false,
    "team_receive": true,
    "specialization_receive": true,
    "group_receive": true,
    "moderate_transmissions": false,
    "administer_accounts": false,
    "administer_memberships": false,
    "administer_roles": false
  }
}
```

`POST /api/v1/admin/roles` erzeugt eine neue Rolle.
`GET`, `PUT` und `DELETE /api/v1/admin/roles/{role-id}` lesen, ersetzen oder
löschen sie. Update und Delete verlangen den Rollen-ETag. Referenzierte Rollen
liefern bei Delete `409 role_in_use`. Die globale `administrator`-Rolle und
ihre Verwaltungsrechte dürfen online nicht gelöscht oder so geändert werden,
dass kein aktiver Administrator verbleibt; dies liefert
`409 last_administrator_protection`.

`snapshot_version` einer Rollenliste ist zugleich die
`role_catalog_version`. Jede Erstellung, Änderung oder Löschung erhöht sie
strikt, unabhängig von der Version der einzelnen Rolle.

### Effektive Rechte

`GET /api/v1/self/effective-permissions` liefert die ausschließlich
serverberechneten Rechte der eigenen aktuellen Membership:

```json
{
  "api_version": "v1",
  "membership_version": 44,
  "role_catalog_version": 8,
  "permissions": {
    "team_transmit": true,
    "specialization_transmit": false,
    "group_transmit": false,
    "team_receive": true,
    "specialization_receive": true,
    "group_receive": true,
    "moderate_transmissions": false,
    "administer_accounts": false,
    "administer_memberships": false,
    "administer_roles": false
  }
}
```

`POST /api/v1/admin/effective-permission-previews` berechnet dieselbe Struktur
für eine noch nicht gespeicherte Kombination:

```json
{
  "player_id": "player-42",
  "hierarchy_id": "hierarchy-main",
  "group_id": "group-alpha",
  "specialization_id": "specialization-red",
  "team_id": "team-red-1",
  "role_assignments": [],
  "expected_hierarchy_version": 12,
  "expected_role_catalog_version": 8
}
```

Die Vorschau ändert keinen Zustand und erzeugt kein Audit-Event. Veraltete
Katalogversionen liefern `409 preview_basis_stale`.

## Moderation

### `GET /api/v1/moderation/transmissions`

Die paginierte Liste ist stabil nach `started_at_unix_ms`, danach
`transmission_id` sortiert. Jeder Eintrag enthält ausschließlich:

```json
{
  "transmission_id": "tx_...",
  "sender_player_id": "player-42",
  "sender_display_name": "Alex",
  "scope": "group",
  "scope_node_id": "group-alpha",
  "started_at_unix_ms": 1785175000000,
  "membership_version": 44
}
```

Moderator und Administrator sehen nur Transmissionen innerhalb ihres aktuell
moderierbaren Hierarchiescopes. Empfänger-IDs, Account-ID, Anmeldename,
Session, Gerät, Grant-Token und Voice-Inhalt werden nicht ausgegeben.

Der bestehende
`POST /api/v1/transmissions/{transmission-id}/interrupt` mit leerem Body bleibt
für vorhandene Clients unverändert. Neue Verwaltungsoberflächen verwenden den
kanonischen Endpunkt
`POST /api/v1/moderation/transmissions/{transmission-id}/interrupt`:

```json
{
  "reason": "Violation of voice policy"
}
```

Der nicht leere Grund ist dort verpflichtend. Der Erfolg enthält zusätzlich
`audit_event_id`. Ein bereits beendeter Vorgang liefert
`409 transmission_not_active`, Rechteverlust `403 forbidden`.

## Administrative Auditabfrage

### `GET /api/v1/admin/audit-events`

Die Liste ist paginiert und aufsteigend nach unveränderlicher
`audit_sequence` sortiert. Erlaubte Filter sind `after_sequence`,
`event_type`, `actor_account_id`, `target_type`, `target_id`,
`from_unix_ms` und `to_unix_ms`. `cursor` darf nicht mit
`after_sequence` kombiniert werden.

```json
{
  "audit_event_id": "audit-101",
  "audit_sequence": 101,
  "occurred_at_unix_ms": 1785175000000,
  "event_type": "account.disabled",
  "actor_account_id": "account-admin",
  "target_type": "account",
  "target_id": "account-42",
  "result": "succeeded",
  "correlation_id": "corr-...",
  "resource_version": 18,
  "reason": "Administrative request"
}
```

Fehlgeschlagene Änderungen werden ebenfalls mit `result: "rejected"`
auditiert, soweit der Akteur sicher authentifiziert wurde. Auditdatensätze
enthalten niemals Credential-, Hash-, Salt-, Einladungs-, Session-,
Erneuerungs-, Reauthentifizierungs- oder Grantwerte. Presence- und
Voice-Inhalte werden nicht auditiert.

Jede erfolgreiche administrative Änderung gibt ihre `audit_event_id` zurück.
Kann das verpflichtende Audit nicht atomar gespeichert werden, schlägt die
Änderung mit `503 audit_unavailable` fehl und wird nicht angewendet.

## Autorisierungsmatrix

| Ressource | Teilnehmer | Moderator | Administrator |
|---|---:|---:|---:|
| eigenes Directory und Presence | ja | ja | ja |
| eigene Account-, Geräte- und Sessiondaten | ja | ja | ja |
| eigene effektive Rechte | ja | ja | ja |
| aktive moderierbare Transmissionen | nein | ja | ja |
| Transmission abbrechen | nein | im eigenen Scope | ja |
| Accountliste und Account-Lifecycle | nein | nein | ja |
| fremde Geräte und Sessions | nein | nein | ja |
| Hierarchie, Membership und Rollen | nein | nein | ja |
| effektive Rechtevorschau | nein | nein | ja |
| Auditabfrage | nein | nein | ja |

Nicht autorisierte Verwaltungsrequests liefern `403 forbidden`, bevor
Ressourcen-ID, Filtertreffer, Version oder Existenz bewertet werden.
`404 not_found` ist erst nach erfolgreicher Autorisierung zulässig. Dadurch
kann ein normaler Teilnehmer fremde Account- oder Verwaltungsmetadaten weder
über Antworttext noch über unterschiedliche Statuscodes abfragen.

## Datenschutz- und Redaktionsinvarianten

1. Directory und Presence sind immer aus der eigenen aktuellen Group
   abgeleitet; es gibt keine frei angegebene Group-ID.
2. Directory enthält keine Account-ID, Anmeldenamen, Geräte, Sessions,
   internen Rollenrichtlinien oder effektiven Rechte.
3. Presence enthält nur den aktuellen zusammengeführten Onlinezustand und
   keine Historie oder Geräteanzahl.
4. Accountlisten sind ausschließlich für Administratoren sichtbar und
   enthalten keine Credentialmetadaten oder Sessionsecrets.
5. Moderationslisten enthalten keine Empfängerliste und keine Account-,
   Geräte- oder Sessiondaten.
6. Audit enthält IDs und notwendige Betriebsmetadaten, aber niemals Secrets,
   Hashes, Voice-Inhalt oder vollständige Authorization-Header.
7. Fehler und Cursor enthalten keine personenbezogenen oder geheimen Werte in
   lesbarer Form.
8. Alle Antworten bleiben `no-store`; Secret-Ausstellungen sind zusätzlich
   `Pragma: no-cache`.

Die vollständige, vor Implementierung auszuführende Testmatrix steht in
[API-01-Vertragstests](api-01-contract-tests.md).
