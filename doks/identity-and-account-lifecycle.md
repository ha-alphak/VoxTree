# Identitäts- und Account-Lebenszyklus

**Entscheidung:** IAM-01<br>
**Stand:** 27. Juli 2026<br>
**Status:** Verbindlich<br>
**Umsetzung:** IAM-02, IAM-03 und API-01

## Zweck und Abgrenzung

Dieses Dokument legt das Zielmodell für lokale, selbst betriebene Accounts
fest. Es ersetzt noch nicht den implementierten dateibasierten
`MultiUserIdentityProvider`; dessen Ablösung ist Gegenstand von IAM-02. Die
HTTP-Felder und Endpunkte werden in API-01 versioniert. Bestehende
Voice-, Membership- und Transmission-Verträge werden nicht stillschweigend
umgedeutet.

Version 1 verwendet eine geschlossene Registrierung:

- Nur ein Administrator darf Accounts oder Einladungen anlegen.
- Es gibt keine offene Selbstregistrierung und keinen anonymen
  Passwort-Reset.
- Der erste Administrator wird einmalig offline erzeugt.
- Ein späterer externer Identity-Provider bleibt über `IIdentityProvider`
  austauschbar.

Nicht Bestandteil von IAM-01 sind die Implementierung des Accountdienstes, die
Verwaltungsoberfläche, Mehrfaktorauthentifizierung, OIDC/OAuth und
E-Mail-Zustellung.

## Festgestellter Ausgangszustand

Der aktuelle Code besitzt bereits geeignete Grenzen:

- `IIdentityProvider` prüft ein opakes externes Credential, Accountstatus,
  Geräterichtlinie und vorgelagerte Rate Limits.
- `IdentitySessionAuthenticator` trennt externe Prüfung und interne,
  gerätegebundene Session-Ausstellung.
- Sessions werden mit Spieler, Gerät und Ablaufzeitpunkt in SQLite gespeichert
  und standardmäßig auf 15 Minuten begrenzt.
- Der HTTP-Adapter akzeptiert externe Credentials nur bei der
  Session-Erstellung.

Der produktionsnahe Adapter liest jedoch langlebige Bearer-Credentials,
Spieler-IDs und Gerätefreigaben im Klartext aus einer TSV-Datei. Accountstatus,
Profile, Passworthashes, Einladungen, Geräte- und Sessionverwaltung,
Credentialwechsel, Deaktivierung und Recovery sind nicht implementiert.
Persistierte Session-IDs liegen derzeit ebenfalls im Klartext vor.

## Verbindliche Architekturentscheidung

### Komponenten und Vertrauensgrenzen

```text
Desktop-Client
  ├── Passwort nur für Anmeldung/Aktivierung/Änderung im Arbeitsspeicher
  ├── kurzlebige Session nur im Arbeitsspeicher
  └── rotierendes Erneuerungs-Secret im OS-Secret-Store
             │ TLS
             ▼
Control-Plane-HTTP-Adapter
             │ typisierte Befehle, keine Credential-Logs
             ▼
LocalIdentityProvider : IIdentityProvider
  ├── Account-/Credential-/Einladungsdienst
  ├── persistente Rate Limits
  └── Session-/Gerätewiderruf
             │ parametrisierte SQLite-Anweisungen
             ▼
SQLite
  ├── Account- und öffentliche Profildaten
  ├── Argon2id-Hashes
  ├── Hashes einmaliger und rotierender Secrets
  └── Audit- und Versionsdaten
```

Der HTTP-Adapter kennt keine Passwort-Hashing- oder Accounttabellen. Der lokale
Adapter implementiert `IIdentityProvider` und zusätzliche, fachlich getrennte
Account-Lifecycle-Schnittstellen. Ein externer Provider darf diese Adapter
später ersetzen, ohne Voice-Autorisierung, Routing oder Client-Core an einen
bestimmten Identity-Anbieter zu koppeln.

### Getrennte fachliche Datensätze

| Datensatz | Inhalt | Darf nicht enthalten |
|---|---|---|
| Account | interne `AccountId`, kanonischer Anmeldename, Zustand, Version und Zeitpunkte | Anzeigename, Membership, Rollen oder Klartext-Credentials |
| Öffentliches Profil | stabile `PlayerId`, `AccountId`, Anzeigename und freigegebene Profilfelder | Anmeldename, Credential- oder Sitzungsdaten |
| Credential | `CredentialId`, `AccountId`, Typ, parametrisierter Hash, Änderungszeitpunkt und Wechselpflicht | Passwort oder Aktivierungscode |
| Einladung | Zweck, Account, Hash des einmaligen Secrets, Ablauf, Verbrauch und Version | Klartext-Secret nach der Ausgabe |
| Gerät | serverseitige Geräte-ID, Account, öffentlicher Gerätename, erster/letzter Kontakt und Widerruf | Passwort oder Erneuerungs-Secret |
| Session | interner Datensatz, Account, Player, Gerät, Secret-Hash, Ablauf, Widerruf und Tokenfamilie | im Client sichtbares Secret |
| Membership | Zuordnung der `PlayerId` zu Group, Specialization und Team | Accountstatus oder Credentialdaten |
| Rollenzuweisung | Subjekt, Rolle, optionaler Hierarchiescope und Version | Passwort- oder Profildaten |

`AccountId`, `PlayerId`, Membership und Rollenzuweisung bleiben auch dann
getrennt, wenn in Version 1 genau ein öffentliches Profil zu einem Account
gehört. Eine Deaktivierung löscht weder Membership noch Auditgeschichte.
Autorisierung wird aus aktuellen Rollenzuweisungen abgeleitet und niemals aus
einem Accountstatus oder einer sichtbaren Cliententscheidung.

Anmeldenamen sind nicht öffentlich. Sie werden als 3 bis 64 Zeichen lange,
kleingeschriebene ASCII-Bezeichner aus `a-z`, `0-9`, `.`, `_` und `-`
kanonisiert. Unicode bleibt dem getrennten Anzeigenamen vorbehalten; dadurch
entstehen bei der Anmeldung keine mehrdeutigen Normalisierungs- oder
Homoglyphenregeln.

### Zustandsmodelle

Ein Account befindet sich in genau einem dieser Zustände:

```text
pending_activation --activate--> active --disable--> disabled
        │                             ▲                 │
        └-----------reinvite----------┘----enable-------┘
```

- `pending_activation` besitzt noch kein verwendbares Passwort.
- `active` darf sich vorbehaltlich Rate Limit, Credential- und Geräterichtlinie
  anmelden.
- `disabled` kann keine Anmeldung, Aktivierung oder Erneuerung durchführen.
  Die Deaktivierung widerruft in derselben Transaktion alle Sessions,
  Erneuerungsfamilien und offenen Einladungen.
- Version 1 löscht Accounts nicht physisch. Eine spätere Lösch- oder
  Anonymisierungsrichtlinie benötigt eine eigene Datenschutzentscheidung.

Credential-Reset ist kein Accountzustand. Er widerruft das bisherige
Credential, alle Sessions und offenen Einladungen und erzeugt eine neue
einmalige Reset-Einladung. Bis zu deren Verbrauch besitzt der Account kein
anmeldefähiges Credential.

Einladungen besitzen die Zustände `pending`, `consumed`, `expired` oder
`revoked`. Der Übergang `pending -> consumed` und das Setzen des neuen
Credentials erfolgen atomar. Parallele oder wiederholte Verwendung liefert
stets denselben öffentlichen Fehler und erzeugt niemals zwei Credentials.

## Passwörter und Hashing

### Bibliothek und Parameter

Der lokale Provider verwendet ausschließlich die High-Level-API von
**libsodium**:

- Argon2id 1.3 über `crypto_pwhash_str_alg(...,
  crypto_pwhash_ALG_ARGON2ID13, ...)`;
- Prüfung über `crypto_pwhash_str_verify`;
- Erneuerung veralteter Parameter nach erfolgreicher Anmeldung über
  `crypto_pwhash_str_needs_rehash`;
- sichere Zufallswerte über `randombytes_buf`;
- Löschen kurzlebiger Klartextpuffer mit `sodium_memzero`; wo die Plattform es
  zulässt, werden diese Puffer zusätzlich mit `sodium_mlock` geschützt.

Der gespeicherte PHC-String enthält Algorithmus, Salt und Parameter. Es wird
weder eigene Kryptografie implementiert noch ein globaler Salt verwendet.
Ein Pepper ist in Version 1 nicht vorgesehen: Er würde einen zusätzlichen
Recovery- und Verfügbarkeits-Root-of-Trust schaffen, ohne einen vollständig
kompromittierten Server zu schützen.

Die Produktionsvorgabe wird auf der Zielhardware kalibriert:

- Zielzeit pro Hash: 250 bis 500 Millisekunden;
- Mindestwerte: 19 MiB Speicher, zwei Iterationen, Parallelität eins;
- Standardstartwert für die Kalibrierung: 64 MiB und drei Iterationen;
- der Dienst verweigert einen Start mit schwächeren konfigurierten Werten;
- die Parameter stehen pro Hash im PHC-String und können ohne
  Passwortzurücksetzung angehoben werden.

Debian 13 verwendet das sicherheitsgepflegte Distributionspaket von libsodium.
Reproduzierbare Windows-/CI-Builds pinnen die für IAM-02 freigegebene Version.
Version und Lizenz erscheinen später in SBOM und Abhängigkeitsdokumentation.

### Passwortregeln

- Mindestens 15 und höchstens 256 Unicode-Codepoints.
- Mindestens 64 Codepoints müssen technisch akzeptiert werden; das höhere
  Produktlimit dient nur einer eindeutigen Ressourcenbegrenzung.
- Leerzeichen und druckbare Unicode-Zeichen sind erlaubt.
- Es gibt keine erzwungenen Zeichenklassen und keinen periodischen Wechsel.
- Vor Aktivierung oder Änderung wird das vollständige Passwort gegen eine
  lokal gepflegte Liste häufiger, erwartbarer und kompromittierter Passwörter
  geprüft. Passwörter werden dafür nie an einen externen Dienst gesendet.
- Ein Wechsel wird nur bei Nutzerwunsch, administrativem Reset oder
  Kompromittierungsverdacht erzwungen.
- Passwörter erscheinen nie in Kommandozeilen, Umgebungsvariablen, Logs,
  Audit-Events, Crashtexten oder Support-Bundles.

Der Server prüft UTF-8 strikt. Die Clients müssen die exakt eingegebene
UTF-8-Folge senden und dürfen Passwörter weder trimmen noch Groß-/Kleinschreibung
ändern. Eine spätere Unicode-Normalisierung wäre eine
Credential-Vertragsänderung und darf nicht still eingeführt werden.

## Provisionierung, Aktivierung und Reset

### Reguläre Accountanlage

1. Ein aktuell autorisierter Administrator legt Account und öffentliches
   Profil an. Anmeldename und `PlayerId` werden eindeutig geprüft.
2. Der Server erzeugt mit einem CSPRNG ein 256-Bit-Einladungssecret und zeigt
   es genau einmal an.
3. Persistiert werden nur ein schneller kryptografischer Hash des zufälligen
   Secrets, Zweck, Account, Ausgabe- und Ablaufzeitpunkt, Fehlversuche und
   Version. Ein Argon2id-Hash ist für ein zufälliges 256-Bit-Secret nicht
   erforderlich.
4. Der Administrator übermittelt das Secret über einen vom Voice-System
   getrennten, angemessenen Kanal. Der Server versendet in Version 1 keine
   E-Mail.
5. Der Nutzer aktiviert den Account mit Anmeldename, Einladungssecret und neuem
   Passwort. Erfolg verbraucht die Einladung atomar, speichert Argon2id und
   aktiviert den Account.

Einladungen laufen standardmäßig nach 24 Stunden ab und sind auf zehn
Fehlversuche begrenzt. Neuausstellung widerruft alle älteren offenen
Einladungen desselben Zwecks.

### Passwortänderung

Ein angemeldeter Nutzer muss das aktuelle Passwort erneut bestätigen. Die
Änderung speichert zuerst den neuen Hash und erhöht anschließend in derselben
Transaktion die Credential-Epoche. Alle anderen Sessions und
Erneuerungsfamilien werden widerrufen; die ausführende Session darf gemäß
expliziter Nutzerbestätigung bestehen bleiben. Ein Kompromittierungsverdacht
widerruft immer auch diese Session.

### Administrativer Reset

Es gibt keinen unauthentifizierten „Passwort vergessen“-Endpunkt. Ein
Administrator:

1. authentifiziert sich für die sensible Aktion erneut;
2. gibt einen nicht leeren Grund an;
3. erzeugt eine einmalige Reset-Einladung;
4. widerruft atomar das alte Credential, alle Sessions, Geräteerneuerungen und
   älteren Einladungen des Zielaccounts.

Antworten und Clienttexte dürfen nicht offenlegen, ob ein beliebiger
Anmeldename existiert. Administratoren arbeiten dagegen mit stabilen internen
Account-IDs aus einer bereits autorisierten Accountliste.

## Geräte und Sessions

Die vom Client gelieferte Installations-ID ist ein Korrelationsmerkmal, kein
Authentifizierungsfaktor. Nach erfolgreicher Anmeldung wird sie einem
serverseitigen Gerätedatensatz zugeordnet.

Der lokale Provider stellt zwei getrennte Secrets aus:

- eine zufällige Zugriffssession mit standardmäßig 15 Minuten Laufzeit, die
  nur im Prozessspeicher des Clients liegt;
- ein an Account, Gerät und Tokenfamilie gebundenes Erneuerungssecret mit
  standardmäßig 30 Tagen Inaktivitäts- und 90 Tagen absoluter Laufzeit.

Beide Secrets enthalten mindestens 256 Bit CSPRNG-Entropie. SQLite speichert
nur indizierbare kryptografische Hashes, niemals die ausgegebenen Werte.
Erneuerungssecrets rotieren bei jeder Verwendung. Die Wiederverwendung eines
bereits verbrauchten Secrets widerruft die gesamte Tokenfamilie als möglichen
Diebstahlnachweis.

Das Erneuerungssecret liegt unter Windows ausschließlich im Credential Manager
und unter Debian/KDE ausschließlich über Secret Service/KWallet. Ohne
verfügbaren Secret-Store gibt es keine dauerhafte Anmeldung; ein Rückfall auf
Konfigurationsdateien ist unzulässig.

Nutzer können eigene Sitzungen und Geräte auflisten und einzeln oder gesammelt
widerrufen. Administratoren können dies für einen Zielaccount. Account-
Deaktivierung, Passwortreset, Recovery und relevante Rollenentzüge widerrufen
betroffene Sessions in derselben fachlichen Transaktion. Ein Zugriffstoken
bleibt auch nach jedem Neustart widerrufen.

## Erster Administrator und Recovery

### Einmaliger Bootstrap

IAM-02 stellt einen Offline-Unterbefehl nach folgendem Vertrag bereit:

```text
hvc-control-plane bootstrap-admin \
  --database <path> \
  --login <name> \
  --display-name <name> \
  --output-secret-file <new-path>
```

Der Befehl:

- startet keinen Netzwerklistener und verlangt exklusiven Datenbankzugriff;
- funktioniert nur, wenn noch kein Account existiert und der dauerhafte
  Bootstrap-Marker nicht verbraucht ist;
- erzeugt Account, Profil, eine getrennte globale
  `administrator`-Rollenzuweisung und eine Bootstrap-Einladung atomar;
- erzeugt das 256-Bit-Secret selbst und schreibt es ausschließlich in eine
  neu angelegte Datei mit restriktiven Rechten;
- schreibt kein Secret auf stdout/stderr;
- setzt den Bootstrap-Marker in derselben Transaktion dauerhaft auf
  `consumed`;
- erzeugt ein Audit-Event `account.bootstrap.created`.

Der Online-Dienst besitzt keinen Bootstrap-Endpunkt. Das bestehende
`--bootstrap-token-file` bleibt ausschließlich ein
Ein-Spieler-Entwicklungsmodus und ist nicht der Account-Bootstrap.

### Verlust aller Administratorzugänge

Recovery ist eine lokale Break-Glass-Prozedur und kein normaler
Verwaltungsablauf:

1. Control Plane stoppen und sicherstellen, dass kein zweiter Prozess die
   Datenbank geöffnet hat.
2. Datenbank und Auditdaten unverändert sichern.
3. Als Betriebssystemkonto mit Zugriff auf Datenbank und Secret-Verzeichnis
   `hvc-control-plane recover-admin --database <path> --account <id>
   --reason-file <path> --output-secret-file <new-path>` ausführen.
4. Der Befehl darf nur einen bereits global als Administrator zugewiesenen
   Account reaktivieren. Fehlt eine solche Zuweisung, wird abgebrochen und ein
   konsistentes Backup wiederhergestellt; eine verdeckte Rolleneskalation ist
   unzulässig.
5. Recovery widerruft Credential, Sessions, Geräteerneuerungen und offene
   Einladungen des Accounts, erzeugt eine kurzlebige einmalige
   Recovery-Einladung und schreibt ein unveränderliches Audit-Event mit
   Grunddatei-Hash.
6. Dienst starten, Einladung einmalig aktivieren, neues Passwort setzen,
   Audit prüfen und Recovery-Datei sicher vernichten.

Die lokale Datenbank- und Dateiberechtigung ist damit der bewusste
Break-Glass-Root-of-Trust. Produktionsbetrieb muss diesen Zugriff auf den
kleinstmöglichen Operatorenkreis beschränken und Backups verschlüsseln.

## Migration der Identity-Datei

Die Klartext-Bearer-Credentials werden nicht zu Passwörtern umgedeutet und
nicht als Passworthash übernommen. Der IAM-02-Migrationsbefehl arbeitet
offline, wiederholbar und zunächst als Dry Run:

1. Control Plane stoppen; Datenbank und Identity-Datei sichern.
2. Eine Zuordnungsdatei mit bisheriger `player_id`, neuem kanonischem
   Anmeldenamen und Anzeigenamen bereitstellen. Geräte-IDs können als
   beschreibende, zunächst widerrufene Geräte importiert werden.
3. Dry Run prüft doppelte Credentials, Spieler, Anmeldenamen, fehlende
   Memberships, Rollenbezüge und unzulässige Zeichen, ohne zu schreiben.
4. Der echte Lauf importiert pro Spieler atomar Account, Profil und
   Einmal-Einladung. Bestehende `PlayerId`, Memberships, Rollen und
   Auditnachweise bleiben unverändert.
5. Nur Hashes der neu erzeugten Aktivierungssecrets werden gespeichert. Die
   Secrets werden einmalig in eine neu angelegte, restriktive Ausgabedatei
   geschrieben.
6. Nach Stichprobenprüfung startet der Dienst ausschließlich im persistenten
   Accountmodus. Dateiadapter und Accountadapter dürfen nie gleichzeitig
   authentifizieren.
7. Nach erfolgreicher Aktivierungs- und Anmeldeabnahme wird die Identity-Datei
   aus dem aktiven Secret-Verzeichnis entfernt und nur gemäß
   Backup-/Aufbewahrungsrichtlinie offline bewahrt beziehungsweise vernichtet.

Vor der ersten Aktivierung ist Rollback durch Wiederherstellen der
gesicherten Datenbank und Identity-Datei möglich. Nach Accountänderungen ist
ein Downgrade untersagt, weil es Credential-, Widerrufs- und Auditdaten
verlieren würde. Ein erneuter Migrationslauf erkennt bereits importierte
`PlayerId` und verändert sie nicht.

## Bedrohungsanalyse

| Bedrohung | Schutzmaßnahmen | Verbleibendes Risiko |
|---|---|---|
| Online-Brute-Force und Credential Stuffing | Persistente Limits pro Account, Quelladresse und Gerät sowie global; Blockliste; Argon2id; generische Fehler; `Retry-After` | Verteilte Angriffe bleiben möglich und benötigen Betriebsalarme |
| Account-Enumeration | Einheitliche öffentliche Fehler und Antwortform; Dummy-Hash für unbekannte Namen; keine Account-ID in Fehlern | Vollständig identische Laufzeiten sind auf Mehrbenutzersystemen nicht beweisbar |
| Hash-Datenbankdiebstahl | Einzelsalts und parametrisierte Argon2id-Hashes; starke Passwortregeln; verschlüsselte, zugriffsbeschränkte Backups | Schwache Nutzerpasswörter können offline geraten werden |
| Ressourcenerschöpfung durch Argon2id | Rate Limit vor dem Hash, begrenzte Auth-Worker und Queue, globale Parallelitätsgrenze, feste Eingabelimits | Zulässige parallele Logins verbrauchen bewusst Speicher und CPU |
| Einladungs-/Reset-Diebstahl oder Replay | 256 Bit Entropie, Hash-at-Rest, kurze Frist, Einmalverbrauch, Fehlversuchslimit, keine Query-Strings oder Logs | Der getrennte Übermittlungskanal bleibt Betreiberverantwortung |
| Sessiondiebstahl auf Client oder Server | TLS, kurze Zugriffssession, Hash-at-Rest, OS-Secret-Store, Rotation und Reuse-Erkennung, Geräte-/Familienwiderruf | Ein kompromittierter laufender Client kann seine aktive Session verwenden |
| Sessiondiebstahl über Reverse Proxy | Proxy entfernt ungeprüfte Forwarding-Header; TLS extern und geschützter interner Link; keine Header-/Body-Logs | Ein vollständig kompromittierter Proxy liegt innerhalb der Betriebs-Trust-Grenze |
| Account-Deaktivierung wird umgangen | Statusprüfung bei Login und Erneuerung; atomarer Widerruf; geschützte Endpunkte prüfen Session weiterhin | Eine bereits laufende Operation muss an Transaktionsgrenzen sauber abbrechen |
| Parallele Aktivierung, Reset oder Adminänderung | SQLite-Transaktion, strikt steigende Versionen, Compare-and-Swap und eindeutige Constraints | Konflikte werden sichtbar abgelehnt und müssen im Client neu geladen werden |
| Administratormissbrauch | Serverautorisierung, erneute Authentifizierung für sensible Aktionen, getrennte Rollen, vollständiges Audit | Ein alleiniger Administrator bleibt ein mächtiger Vertrauensanker |
| Verlust des einzigen Administrators | Offline-Recovery nur für bestehende Adminzuweisung, exklusiver DB-Zugriff, Secret-Datei und Audit | Verlust von DB/Backups und Operatorzugang ist nicht im Produkt selbst behebbar |
| SQL-Injection und beschädigte Eingaben | Typisierte Befehle, parametrisierte Anweisungen, Längen-/UTF-8-Prüfung, unbekannte Felder ablehnen | Fehler in Adaptercode bleiben durch Tests und Review zu adressieren |
| Geheimnisse in Diagnose oder Crashdaten | Redaktion an der Erzeugungsgrenze; keine Credentialwerte in Exceptions, Logs, Audit oder Support-Bundle | Prozessspeicher kann bei Hostkompromittierung ausgelesen werden |

Rate Limits dürfen keine dauerhafte Account-Sperre durch einen Angreifer
ermöglichen. Fehlversuche verwenden ansteigende Verzögerungen und Zeitfenster;
ein Administrator kann einen Limitzustand auditiert zurücksetzen. Die
Quelladresse stammt nur von einem explizit vertrauten Proxy, der eingehende
Forwarding-Header bereinigt.

## Audit, Datenschutz und Betrieb

Mindestens folgende Ereignisse werden mit UTC-Zeit, Akteur, Ziel,
Ergebnis, Korrelations-ID und fachlicher Version auditiert:

- Accountanlage, Aktivierung, Aktivierungsfehler und Neuausstellung;
- Passwortänderung und administrativer Reset;
- Aktivierung, Deaktivierung und Reaktivierung eines Accounts;
- Login-Erfolg sowie gedrosselte oder abgelehnte Login-Kategorie;
- Session-/Geräteausstellung, Rotation, Reuse-Erkennung und Widerruf;
- Bootstrap und Offline-Recovery;
- Migration und Rollback-Versuch;
- Rollenänderungen über die getrennte Autorisierungsgrenze.

Audit- und Diagnosedaten enthalten niemals Passwort, Hash, Salt,
Einladungssecret, Session-/Erneuerungssecret oder vollständige
Authorization-Header. Fehlgeschlagene Passwörter werden weder gespeichert noch
verglichen protokolliert. Login-Namen und Gerätebezeichnungen sind
personenbezogene Betriebsdaten und unterliegen Zugriffskontrolle und später
festzulegender Aufbewahrung.

Backups müssen Account-, Membership-, Rollen- und Auditdaten konsistent
erfassen, verschlüsselt sein und in IAM-02 durch einen
Backup-/Restore-Test mit anschließender Session-Widerrufsprüfung qualifiziert
werden.

## Anforderungen an API-01 und IAM-02

API-01 muss vor Clientimplementierung mindestens versionieren:

- Anmeldung, Erneuerung und Abmeldung;
- Aktivierung und Passwortänderung;
- Account-, Einladungs-, Geräte- und Sessionverwaltung;
- Deaktivierung/Reaktivierung und administrativen Reset;
- strikt generische öffentliche Authentifizierungsfehler;
- Versionskonflikte, Paging und Auditbezug.

Bestehende `/api/v1`-Felder werden nicht umgedeutet. Der statische
Identity-Dateimodus bleibt bis zur Migration als expliziter Entwicklungs- und
Rollbackpfad verfügbar, kann aber nicht gleichzeitig mit dem persistenten
Accountadapter laufen. API-01 dokumentiert den Client-Migrationspfad.

IAM-02 muss automatisiert prüfen:

- Account-, Einladungs-, Credential-, Geräte-, Session- und Auditmigrationen;
- alle Zustandsübergänge, Versionen und Parallelkonflikte;
- Argon2id-Erzeugung, Prüfung und Rehash;
- Rate Limits vor teurer Hasharbeit;
- Secret-Hashing, Rotation, Reuse-Erkennung und atomaren Widerruf;
- Bootstrap genau einmal sowie Recovery nur für bestehende Administratoren;
- Identity-Datei-Dry-Run, Import, Wiederholung und Rollbackgrenze;
- Redaktionsregeln gegen bekannte Credentials, Sessions und Tokens.

## Abnahme IAM-01

| Kriterium | Festlegung |
|---|---|
| Geschlossene Provisionierung | Nur Administratoren erzeugen Accounts und Einladungen; keine Selbstregistrierung |
| Erster Administrator | Einmaliger Offline-Bootstrap mit dauerhaftem Marker und separater Rollenzuweisung |
| Credential-Lebenszyklus | Aktivierung, Änderung und administrativer Reset mit Argon2id und atomarem Widerruf |
| Account-Deaktivierung | Getrennter Zustand; Membership, Rollen und Audit bleiben erhalten |
| Geräte und Sessions | Hash-at-Rest, kurze Session, rotierendes Erneuerungssecret und gezielter Widerruf |
| Getrennte Datenmodelle | Account, Profil, Membership und Rollen sind eigenständig versioniert |
| Lokaler Adapter | SQLite-basierter `IIdentityProvider`; externer Provider bleibt austauschbar |
| Bedrohungsanalyse | Anmeldung, Aktivierung, Reset, Brute Force, Sessiondiebstahl und Administratorverlust behandelt |
| Migration | Offline-Dry-Run und Import ohne Übernahme der Klartext-Bearer als Passwort |
| Recovery | Dokumentierte Break-Glass-Prozedur für einen bestehenden Administrator |

## Normative Sicherheitsquellen

- [NIST SP 800-63B-4, Authentication and Authenticator Management](https://pages.nist.gov/800-63-4/sp800-63b.html)
- [libsodium: Password hashing](https://doc.libsodium.org/password_hashing)
- [libsodium: The `pwhash*` API](https://doc.libsodium.org/password_hashing/default_phf)
- [OWASP Password Storage Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html)
- [OWASP Session Management Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/Session_Management_Cheat_Sheet.html)
- [RFC 9700: Best Current Practice for OAuth 2.0 Security](https://www.rfc-editor.org/rfc/rfc9700.html), herangezogen für Rotation und Reuse-Erkennung langlebiger Erneuerungssecrets; HVC implementiert damit kein OAuth-Protokoll.
