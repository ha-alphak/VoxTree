# API-01-Vertragstests

**Stand:** 27. Juli 2026  
**Bezug:** [Directory-, Identity- und Verwaltungsvertrag v1](network-contract-v1-api-01.md)  
**Status:** Verbindliche Abnahmespezifikation für DIR-01, IAM-02, IAM-03,
ADM-01 und MOD-01

## Testregeln

Jeder später implementierte Endpunkt wird auf Anwendungsschicht und
HTTP-Adapter getestet. Sicherheitsrelevante Negativfälle werden zusätzlich
gegen den realen Linux-Listener ausgeführt. Tests verwenden mindestens zwei
Groups, je zwei Specializations und Teams sowie Teilnehmer-, Moderator- und
Administratorsessions.

Jeder HTTP-Test prüft mindestens Status, `Content-Type`, `Cache-Control`,
`X-HVC-API-Version`, `Content-Length`, Fehlercode und das Fehlen verbotener
Felder. Secretprüfungen verwenden eindeutige Marker für Passwort, Einladung,
Zugriffssession, Erneuerung, Reauthentifizierung und Voice-Grant.

## Kompatibilität

| ID | Fall | Erwartung |
|---|---|---|
| API-COMP-01 | Bestehendes opakes Bearer an `POST /sessions` im Dateimodus | unveränderte `201`-Antwort ohne neuen Passwort- oder Refreshvertrag |
| API-COMP-02 | Body oder Passwortfelder an `POST /sessions` | deterministische Ablehnung; keine Umdeutung |
| API-COMP-03 | Bestehende Membership-, Voice-Grant- und Transmissiontests | unverändert grün |
| API-COMP-04 | Persistenter Modus ruft Legacy-Session auf | `410 legacy_identity_mode_disabled` |
| API-COMP-05 | Dateimodus ruft persistente Accountressource auf | `503 persistent_identity_unavailable` |
| API-COMP-06 | Neuer Membership-Body verwendet Rollenarray | nur am neuen `/admin/players/{id}/membership`-Pfad akzeptiert |

## Directory und Presence

| ID | Fall | Erwartung |
|---|---|---|
| API-DIR-01 | Teilnehmer liest eigenes Directory | genau eine sichtbare Group, vollständige Knoten und sichtbare Teilnehmer |
| API-DIR-02 | Teilnehmer zweier Groups lesen parallel | keine ID, Rolle, Version oder Profiländerung der jeweils fremden Group |
| API-DIR-03 | Manipulierter Pfad oder Query versucht fremde Group-ID | `404 route_not_found` oder `400 invalid_request`, niemals fremde Daten |
| API-DIR-04 | Directory-Antwort wird nach verbotenen Feldern durchsucht | keine Account-, Login-, Credential-, Geräte-, Session-, IP- oder Auditdaten |
| API-DIR-05 | Interne, nicht öffentliche Rolle | Rolle fehlt im öffentlichen Profil und Rollenkatalog |
| API-DIR-06 | Sichtbare Profil-, Membership- oder Hierarchieänderung | strikt höhere `directory_version` und neuer ETag |
| API-DIR-07 | Passender `If-None-Match` | `304`, leerer Body und keine veralteten Teilnehmerdaten |
| API-DIR-08 | Mehr als 200 sichtbare Teilnehmer | `413 directory_limit_exceeded`, keine Teilantwort |
| API-PRE-01 | Vollständiger Presence-Snapshot | jeder sichtbare Spieler genau einmal als `online` oder `offline` |
| API-PRE-02 | Derselbe Spieler ist in mehreren Voice-Scopes verbunden | genau ein zusammengeführter Onlineeintrag |
| API-PRE-03 | Delta nach bekannter Version | nur neueste sichtbare Änderung je Spieler und strikt höhere Version |
| API-PRE-04 | Zu alte, null oder zukünftige Version | `409 presence_snapshot_required` |
| API-PRE-05 | Fremde Group ändert Presence | weder Eintrag noch erkennbare Versionsänderung in der eigenen Sicht |
| API-PRE-06 | Presence-JSON wird auf Historie geprüft | kein Last-Seen, keine Geräteanzahl, keine IP und kein Sprechzustand |

## Öffentliche Authentifizierung und Secrets

| ID | Fall | Erwartung |
|---|---|---|
| API-AUTH-01 | Gültiger Login | kurzlebige Session und einmaliges rotierbares Erneuerungssecret |
| API-AUTH-02 | Unbekannter Name, falsches Passwort, deaktivierter Account | identischer Status, Code und öffentliche JSON-Form |
| API-AUTH-03 | Gültige, abgelaufene, verbrauchte und widerrufene Einladung | nur Erfolg unterscheidet sich; alle Ablehnungen sind `authentication_failed` |
| API-AUTH-04 | Aktivierung und Credentialanlage laufen parallel | genau ein atomarer Erfolg, kein zweites Credential |
| API-AUTH-05 | Refresh rotiert das Secret | altes Secret nicht erneut verwendbar |
| API-AUTH-06 | Verbrauchtes Refresh-Secret wird wiederverwendet | gesamte Familie widerrufen, öffentlich nur `authentication_failed` |
| API-AUTH-07 | Öffentlicher Authrequest trägt `Authorization` | `400 unexpected_authorization` |
| API-AUTH-08 | Ungültiges UTF-8, unbekanntes oder doppeltes Feld | `400 invalid_request` vor Accountsuche |
| API-AUTH-09 | Account- und globales Rate Limit | `429`, begrenztes `Retry-After`, keine Accountmetadaten |
| API-AUTH-10 | Passwortpolicy scheitert unabhängig vom Account | `422 password_policy_violation`, kein Passwort im Fehler |
| API-SEC-01 | Logs, Audit, Fehler und Exceptions werden nach Markern durchsucht | kein Passwort, Einladungs-, Session-, Refresh-, Reauth- oder Grantwert |
| API-SEC-02 | Secret-Ausstellungsantwort | `Cache-Control: no-store` und `Pragma: no-cache` |
| API-SEC-03 | Reauthentifizierungsnachweis wird zweimal verwendet | erster sensibler Request kann gelingen, zweiter `reauthentication_required` |
| API-SEC-04 | Reauthentifizierungsnachweis an anderer Session/Gerät | `reauthentication_required` |

## Eigene Ressourcen

| ID | Fall | Erwartung |
|---|---|---|
| API-SELF-01 | Eigener Account wird gelesen | nur eigener Account und eigener öffentlicher Profilbezug |
| API-SELF-02 | Profilupdate mit passendem ETag | neue Profilversion, Accountversion unverändert |
| API-SELF-03 | Passwortwechsel mit passendem Account-ETag | neues Credential, alte aktuelle und andere Sessions widerrufen; optional neue Ersatzsession |
| API-SELF-04 | Passwortwechsel mit `keep_current_session=false` | Antwort wird abgeschlossen, danach ist aktuelle Session ungültig |
| API-SELF-05 | Geräte- und Sessionlisten | keine verwendbare Session- oder Refreshkennung |
| API-SELF-06 | Fremde Geräte-/Session-ID unter `/self` | `404 not_found`, keine Besitzinformation |
| API-SELF-07 | Aktuelles Gerät ohne Bestätigung widerrufen | `400 current_resource_confirmation_required` |
| API-SELF-08 | Gerätewiderruf | Gerät, Sessions und Refreshfamilien atomar widerrufen |
| API-SELF-09 | Alle anderen Sessions werden widerrufen | aktuelle Session bleibt, jede andere Session und Refreshfamilie endet atomar |

## Autorisierung und Datenschutz

| ID | Fall | Erwartung |
|---|---|---|
| API-AZ-01 | Teilnehmer ruft jeden `/admin`-Endpunkt direkt auf | `403 forbidden` vor ID-, Filter- oder Versionsauswertung |
| API-AZ-02 | Moderator ruft Account-, Rollen-, Membership- oder Auditverwaltung auf | `403 forbidden` |
| API-AZ-03 | Moderator listet aktive Transmissionen | nur aktuell moderierbarer Scope |
| API-AZ-04 | Moderator versucht fremden Scope abzubrechen | `403 forbidden`, Transmission bleibt aktiv |
| API-AZ-05 | Administrator verliert Rolle zwischen Lesen und Schreiben | Mutation `403`, auch bei passendem ETag |
| API-AZ-06 | Deaktivierte oder widerrufene Session | geschützte Ressource wird vor Fachlogik abgelehnt |
| API-PRIV-01 | Accountliste | keine Hashes, Credential-, Session- oder Refreshwerte |
| API-PRIV-02 | Moderationsliste | keine Empfänger-, Account-, Login-, Geräte-, Session- oder Grantdaten |
| API-PRIV-03 | Auditliste | keine Secrets, Hashes, Authorization-Header oder Voice-Inhalte |
| API-PRIV-04 | Fehlervergleich existierende/nicht existierende Adminressource durch Nichtadmin | identische `403`-Form |

## Versionierung, Paging und Konflikte

| ID | Fall | Erwartung |
|---|---|---|
| API-VER-01 | Mutation ohne `If-Match` | `428 precondition_required`, keine Änderung, kein Erfolgs-Audit |
| API-VER-02 | Mutation mit altem ETag | `412 version_conflict`, aktueller ETag, kein verlorenes Update |
| API-VER-03 | Zwei Admins ändern dieselbe Ressource parallel | genau einer gewinnt; der andere erhält Konflikt |
| API-VER-04 | Erfolgreiche Mutation | Version strikt erhöht und neuer ETag stimmt mit Antwort überein |
| API-PAGE-01 | Standard-, Minimal- und Maximallimit | 50, 1 und 200 Einträge; außerhalb `400 invalid_paging` |
| API-PAGE-02 | Alle Seiten eines stabilen Snapshots | keine Lücke und kein Duplikat |
| API-PAGE-03 | Cursor wird verändert oder mit anderen Filtern verwendet | `400 invalid_cursor` |
| API-PAGE-04 | Cursor wird von anderem Akteur verwendet | `403 cursor_forbidden` |
| API-PAGE-05 | Snapshot ist nicht mehr verfügbar | `409 page_snapshot_changed`, Neustart ohne Cursor |
| API-PAGE-06 | Cursor wird auf Klartextdaten und Secretmarker geprüft | keine lesbaren personenbezogenen oder geheimen Werte |

## Account-, Hierarchie-, Membership- und Rollenverwaltung

| ID | Fall | Erwartung |
|---|---|---|
| API-ADM-01 | Administrator legt Account an | Account, Profil und Einladung atomar; Secret genau einmal in der Antwort |
| API-ADM-02 | Doppelte Login- oder Player-ID | `409 account_identity_conflict`, keine Daten des vorhandenen Accounts |
| API-ADM-03 | Einladung wird neu ausgestellt | ältere offene Einladung desselben Zwecks widerrufen |
| API-ADM-04 | Account wird deaktiviert | Credential, Sessions, Refreshfamilien und Einladungen atomar widerrufen; Membership bleibt |
| API-ADM-05 | Credentialreset | alter Zugriff widerrufen, genau eine neue Reset-Einladung |
| API-ADM-06 | Sensible Aktion ohne Reauthentifizierung | `401 reauthentication_required`, keine Teiländerung |
| API-ADM-07 | Einladungen werden gelistet oder einzeln widerrufen | Zustände und Versionen sichtbar, niemals Secret oder Hash; Widerruf auditiert |
| API-ADM-08 | Öffentliches Profil wird administrativ geändert | Profilversion und Directory-Version steigen, Accountversion bleibt getrennt |
| API-HIER-01 | Gültiger Hierarchiekatalog | atomarer Replace und strikt höhere Version |
| API-HIER-02 | Zyklus, falscher Elterntyp oder doppelte Node-ID | `422 invalid_hierarchy`, alter Katalog bleibt vollständig |
| API-HIER-03 | Referenzierter Knoten wird entfernt | `409 hierarchy_in_use`, Membership unverändert |
| API-HIER-04 | Neue Hierarchie mit eindeutiger ID | Version eins, ETag und genau ein Audit |
| API-MEM-01 | Membershipwechsel mit gültigen Bezügen | atomar neue Membership; aktive Transmission endet |
| API-MEM-02 | Rolle passt nicht zum Scope | `422 invalid_role_scope`, keine Teiländerung |
| API-MEM-03 | Membership wird entfernt | Account und Profil bleiben, aktive Transmission endet |
| API-MEM-04 | Moderations- oder Verwaltungsrecht ändert sich | alle Sessions und Refreshfamilien des betroffenen Accounts widerrufen |
| API-ROLE-01 | Rolle und Richtlinie werden erstellt/geändert | getrennte Version, Audit-ID und serverberechnete Rechte |
| API-ROLE-02 | Referenzierte Rolle wird gelöscht | `409 role_in_use` |
| API-ROLE-03 | Letzter aktiver Administrator würde entzogen | `409 last_administrator_protection` |
| API-PERM-01 | Eigene effektive Rechte | entsprechen aktueller Membership und aktuellem Rollenkatalog |
| API-PERM-02 | Vorschau mit veralteter Basis | `409 preview_basis_stale`, keine Mutation und kein Audit |
| API-PERM-03 | Client sendet manipulierte effektive Rechte | Werte werden ignoriert oder als unbekannte Felder abgelehnt; Server berechnet selbst |

## Moderation und Audit

| ID | Fall | Erwartung |
|---|---|---|
| API-MOD-01 | Aktive Transmission wird gelistet und über den neuen Moderationspfad autorisiert abgebrochen | stabiler Eintrag, terminaler Abbruch, Audit-ID; bestehender leerer v1-Abbruch bleibt kompatibel |
| API-MOD-02 | Bereits beendete Transmission | `409 transmission_not_active`, kein zweiter Abbruch |
| API-MOD-03 | Leerer Moderationsgrund am neuen Moderationspfad | `422 reason_required` |
| API-AUD-01 | Jede erfolgreiche Adminmutation | genau ein Audit mit Akteur, Ziel, Zeit, Ergebnis, Korrelation und neuer Version |
| API-AUD-02 | Authentifizierte abgelehnte Adminmutation | Audit mit `result: rejected`, ohne Secret oder Requestbody |
| API-AUD-03 | Verpflichtendes Audit kann nicht gespeichert werden | `503 audit_unavailable`, Fachmutation wird zurückgerollt |
| API-AUD-04 | Audit-Paging nach Sequenz | stabile, streng steigende Reihenfolge ohne Duplikate |

## Abnahmezuordnung API-01

| Abnahmekriterium | Nachweis in dieser Spezifikation |
|---|---|
| Positivtests spezifiziert | Erfolgsfälle in allen Abschnitten |
| Negativtests spezifiziert | Syntax-, Zustand-, Konflikt- und Integritätsfälle |
| Datenschutztests spezifiziert | `API-PRIV-*`, `API-SEC-*`, `API-DIR-04`, `API-PRE-06` |
| Autorisierungstests spezifiziert | `API-AZ-*` und Rollenmatrix des Vertrags |
| Keine fremden Groups oder Verwaltungsmetadaten | `API-DIR-02/03`, `API-AZ-01/02`, `API-PRIV-04` |
| Keine Account-Enumeration | `API-AUTH-02/03/09` |
| Bestehende Voice-v1-Verträge kompatibel | `API-COMP-01` bis `API-COMP-06` |
