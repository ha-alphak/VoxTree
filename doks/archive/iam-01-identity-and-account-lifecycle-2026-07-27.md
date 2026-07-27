# IAM-01: Identitäts- und Account-Lebenszyklus

**Abgeschlossen:** 27. Juli 2026<br>
**Arbeitspaket:** IAM-01<br>
**Ergebnis:** Abgenommen

## Umfang

IAM-01 entscheidet den Identitäts- und Account-Lebenszyklus vor seiner
Implementierung. Der aktuelle dateibasierte Identity-Adapter und die
bestehenden `/api/v1`-Endpunkte bleiben unverändert implementiert; IAM-02,
IAM-03 und API-01 setzen die Entscheidung um.

Die verbindliche Spezifikation steht in
[../identity-and-account-lifecycle.md](../identity-and-account-lifecycle.md).

## Entscheidungen

- Geschlossene administrative Provisionierung ohne Selbstregistrierung oder
  anonymen Passwort-Reset.
- Getrennte, versionierte Datensätze für Account, öffentliches Profil,
  Membership und Rollenzuweisung.
- Lokaler SQLite-Adapter hinter der bestehenden `IIdentityProvider`-Grenze.
- Argon2id 1.3 ausschließlich über die High-Level-API von libsodium mit
  eingebetteten Parametern, Mindestkosten und produktiver Kalibrierung.
- Einmalige 256-Bit-Secrets für Einladung, Aktivierung, Reset, Bootstrap und
  Recovery; nur Hashes werden persistiert.
- Kurzlebige Zugriffssessions und rotierende, gerätegebundene
  Erneuerungssecrets mit Hash-at-Rest und Reuse-Erkennung.
- Einmaliger Offline-Bootstrap des ersten Administrators sowie auditierte
  Break-Glass-Recovery ohne verdeckte Rolleneskalation.
- Offline-Dry-Run und Migration der Identity-Datei unter Erhalt von
  `PlayerId`, Memberships, Rollen und Auditgeschichte. Die Klartext-Bearer
  werden nicht zu Passwörtern.

## Abnahmekriterien

| Kriterium | Nachweis |
|---|---|
| Bedrohungsanalyse für Anmeldung und Brute Force | Persistente mehrdimensionale Rate Limits, Argon2id, Blockliste, generische Fehler, Dummy-Hash und begrenzte Auth-Worker spezifiziert |
| Aktivierung und Reset | Atomare Einmal-Secrets, Ablauf, Fehlversuchslimit, Neuausstellung und vollständiger Widerruf spezifiziert |
| Sessiondiebstahl | TLS, Hash-at-Rest, kurze Zugriffssession, OS-Secret-Store, Rotation, Reuse-Erkennung und Gerätewiderruf spezifiziert |
| Administratorverlust | Offline-Recovery für eine bestehende Adminzuweisung mit exklusivem DB-Zugriff, Backup, Secret-Datei und Audit festgelegt |
| Identity-Datei-Migration | Dry Run, Zuordnung, transaktionaler Import, Aktivierungssecrets, Cutover und Rollbackgrenze dokumentiert |
| Erster Administrator | Einmaliger Offline-Bootstrap mit dauerhaftem Marker und separater Rollenzuweisung festgelegt |

## Validierung

- Dokumentation gegen `IIdentityProvider`,
  `IdentitySessionAuthenticator`, `MultiUserIdentityProvider`, SQLite-Sessions
  und den HTTP-v1-Vertrag abgeglichen.
- Windows Debug und Release: jeweils vollständige 15 von 15 CTest-Tests
  bestanden; darin `application.identity`,
  `persistence.sqlite_control_plane` und `network.control_plane_http`.
- Markdown-Links und UTF-8, Sensitive-Content-Scan sowie
  `git diff --check`: bestanden.

Es wurden keine Produktionsdaten migriert und keine Accounts angelegt.
Native Audio-, UI- und Hardwaretests sind für die reine
Architekturentscheidung nicht einschlägig.

## Verbleibender Umfang

- API-01 versioniert die Directory-, Presence-, Account- und
  Verwaltungsverträge.
- IAM-02 implementiert Persistenz, libsodium, Lifecycle, Rate Limits,
  Bootstrap, Recovery und Migration.
- IAM-03 implementiert Aktivierung, Anmeldung und Selbstverwaltung in den
  Clients.
- Passwortkosten werden in IAM-02 auf der Debian-Produktionshardware
  kalibriert; Secret-Store-Adapter werden in IAM-03 plattformspezifisch
  qualifiziert.
