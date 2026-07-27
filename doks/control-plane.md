# Linux-Control-Plane

**Stand:** 27. Juli 2026

## Modulgrenzen

Die Control-Plane besteht aus vier Targets:

- `hvc-application` enthält transport- und frameworkunabhängige
  Anwendungs-Schnittstellen, die serverseitige Autorisierung sowie vollständige
  Start- und End-Anwendungsfälle für Transmissionen.
- `hvc-control-plane` ist der ausführbare Server-Entry-Point. Netzwerkprotokoll,
  Datenbankmigration, Adapterverdrahtung und Linux-Listener werden hier
  zusammengeführt.
- `hvc-network` bildet den versionierten HTTP-v1-Vertrag auf die
  frameworkunabhängigen Anwendungsschnittstellen ab.
- `hvc-persistence` bindet SQLite als dauerhafte Ablage an.

Die Anwendungsschicht verwendet `hvc-domain` für die autoritative
Empfängerermittlung. Sie hängt weder von LiveKit noch von einem HTTP-Framework
oder einer Datenbank ab.

`hvc-persistence` ist ein Adaptermodul. Es hängt von `hvc-application` ab und
stellt mit `SqliteControlPlaneRepository` die dauerhafte Implementierung für
Sessions, autoritative Membership-Kontexte und Transmission-Audit-Events
bereit. Der bisherige Name `SqliteSessionRepository` bleibt als
Quellcode-Kompatibilitätsalias erhalten.

## Authentifizierte Sessions

`ISessionAuthenticator` bildet einen externen Berechtigungsnachweis und eine
Geräte-ID auf eine `AuthenticatedSession` ab. Eine Session bindet serverseitig:

- `SessionId`
- `PlayerId`
- `DeviceId`
- Ablaufzeitpunkt

`ISessionRepository` stellt bereits ausgestellte Sessions für
Anwendungsfälle bereit. Eine Transmission wird abgelehnt, wenn die Session
fehlt, abgelaufen ist oder nicht zum anfragenden Gerät gehört.

`IMutableSessionRepository` ergänzt die Schreiboperationen `upsert` und
`erase`. Der SQLite-Adapter implementiert diese Schnittstelle und speichert
Session-, Spieler- und Geräte-ID sowie den Ablaufzeitpunkt. Er kann nach dem
Schließen mit derselben Datenbankdatei erneut geöffnet werden, ohne die
Sessions zu verlieren.

`IdentitySessionAuthenticator` trennt die produktive Identity-Anbindung in zwei
Verantwortlichkeiten. `IIdentityProvider` prüft das opake externe Credential,
den Accountstatus, die Geräterichtlinie und vorgelagerte Rate Limits.
`ISessionIdGenerator` erzeugt davon unabhängig die interne Session-ID. Die
ausgestellte Laufzeit ist stets das Minimum aus der vom Provider zugelassenen
Laufzeit und der lokalen `IdentitySessionPolicy`. Externe Tokens werden dadurch
weder als interne Session-ID wiederverwendet noch persistiert.

## Schema-Migrationen

`SqliteControlPlaneRepository` migriert seine Datenbank beim Öffnen auf die
aktuell unterstützte Schemaversion. Die erste Migration legt an:

- `schema_migrations` als nachvollziehbare Historie,
- `sessions` als dauerhafte Session-Ablage,
- Indizes für Spieler und Ablaufzeitpunkt.

Jede Migration läuft innerhalb von `BEGIN IMMEDIATE` und `COMMIT`; bei einem
Fehler erfolgt ein Rollback. `PRAGMA user_version` ist die verbindliche
Schemaversion. Fehlende Versionsschritte und neuere, vom Programm nicht
unterstützte Datenbanken werden abgelehnt.

Die zweite Migration ergänzt normalisierte Tabellen für:

- den Kontextbesitzer, die Snapshot-Version und die Hierarchie-ID,
- Scope-, Gruppen-, Specialization- und Team-Definitionen,
- Memberships einschließlich Verbindungs-, Mute- und Ban-Status,
- Rollenzuweisungen sowie getrennte Sende- und Empfangsrechte.

Ein vollständiger Kontext wird mit allen abhängigen Datensätzen in einer
SQLite-Transaktion ersetzt. Der Adapter vergleicht die als vorzeichenlosen
64-Bit-Wert gespeicherte Version innerhalb derselben Transaktion und lehnt
gleiche oder ältere Versionen ab.

Die dritte Migration ergänzt `transmission_audit_events`. Eine automatisch
vergebene, niemals wiederverwendete Sequenz bildet die verbindliche
Einfügereihenfolge. Ein Zeitindex unterstützt spätere Aufbewahrungsjobs. Das
Schema enthält alle typisierten Audit-Felder einschließlich Empfängeranzahl,
aber keine Spalte für interne Empfänger-IDs.

Die ausführbare Control-Plane öffnet die Datenbank beim Start und führt damit
die Migrationen vor allen späteren Netzwerkdiensten aus. Standardmäßig wird
`hvc-control-plane.db` verwendet; mit `--database <path>` kann ein anderer
Pfad gewählt werden.

## Autoritative Membership

`IAuthoritativeMembershipProvider` liefert für einen authentifizierten Spieler
einen konsistenten Anwendungskontext aus:

- einem immutablen, versionierten `MembershipSnapshot`
- dazugehöriger `RolePolicy`

Beide Objekte werden zusammen bezogen, damit die Transmission nicht gegen eine
Membership-Version und eine davon abweichende Rollenrichtlinie geprüft wird.
`IMutableAuthoritativeMembershipRepository` ergänzt atomare
Compare-and-Replace- sowie Löschoperationen. Der `InMemoryControlPlaneStore`
kann diese Schnittstelle als persistente Quelle verwenden. Er hält sein
Store-Lock über das erfolgreiche SQLite-Update und den Abbruch einer aktiven
Transmission desselben Spielers. Startaktivierung, Membership-Lesen und
Membership-Änderung sehen dadurch entweder vollständig den alten oder
vollständig den neuen Zustand.

## Transmission-Autorisierung

`StartTransmissionCommand` enthält ausschließlich:

- Session- und Geräte-ID
- Client-Transmission-ID
- angeforderten Scope
- bekannte Membership-Version
- Korrelations-ID

Der Befehl enthält absichtlich weder eine Sender-ID noch eine Empfängerliste.
`TransmissionAuthorizationService` leitet den Sender aus der authentifizierten
Session ab und berechnet die Empfänger aus dem autoritativen Membership-Kontext.
Eine erfolgreiche Autorisierung erzeugt über `ITransmissionIdGenerator` eine
serverseitige Transmission-ID und gibt den internen Routing-Grantsatz zurück.
`TransmissionApplicationService` registriert diesen Satz anschließend als
aktive Transmission. Dabei werden Session, Gerät und Membership-Version
atomar erneut geprüft, um Änderungen zwischen Autorisierung und Aktivierung
sicher abzufangen.

## Aktive Transmissionen

`IActiveTransmissionRepository` kapselt die atomare Aktivierung und Beendigung.
Der konkrete `InMemoryControlPlaneStore` implementiert gemeinsam:

- Session-Ablage und gerätegebundenes Entfernen,
- autoritative Membership-Kontexte,
- die Registry aktiver Transmissionen.

Pro Spieler ist höchstens eine aktive Transmission zulässig. Ein Endbefehl
darf nur über die Session und das Gerät erfolgen, mit denen die Transmission
gestartet wurde. Membership- und Rechteänderungen mit höherer Version sowie das
Entfernen einer Session beenden betroffene Transmissionen atomar und liefern
den jeweiligen Abbruchgrund zurück.

## Lebenszyklusregeln

`TransmissionApplicationService` wendet konfigurierbare, frameworkunabhängige
Lebenszyklusregeln an:

- `ITransmissionRateLimiter` begrenzt Start- und Endanforderungen getrennt pro
  authentifiziertem Spieler. Die In-Memory-Implementierung verwendet
  unabhängige Sliding Windows für beide Aktionen.
- `TransmissionLifecyclePolicy` legt eine positive Maximaldauer fest.
  `expireTimedOut` beendet alle zu diesem Zeitpunkt überfälligen
  Transmissionen atomar mit dem Grund `timed_out`.
- `ITransmissionModerationAuthorizer` entscheidet unabhängig vom
  Transportadapter, ob ein authentifizierter Spieler eine konkrete
  Transmission unterbrechen darf. Nur autorisierte Anforderungen enden sie
  atomar mit dem Grund `moderation_interrupted`.

Alle drei Anwendungsfälle übernehmen einen serverseitigen Zeitpunkt und
Korrelations-IDs. Ein späterer Scheduler sowie die dauerhafte Rate-Limit- und
Moderationsrichtlinie bleiben Adapteraufgaben.

## Audit-Events

`ITransmissionAuditEventSink` bindet Audit-Adapter synchron und ohne Abhängigkeit
von Logging-Framework, Netzwerkprotokoll oder Persistenztechnologie an. Der
Anwendungskern erzeugt typisierte Events für:

- erfolgreiche Starts,
- reguläre Enden,
- abgelehnte Start-, End- und Moderationsanforderungen,
- erzwungene Abbrüche durch Moderation oder Timeout.

Der `InMemoryControlPlaneStore` ergänzt erzwungene Abbrüche, die atomar durch
Session-Entfernung sowie Membership- oder Rechteänderungen entstehen. Für eine
vollständige Audit-Spur wird derselbe Sink an Anwendungskern und Store
übergeben.

Ein Event enthält je nach Vorgang Session-, Geräte-, Client-Transmission-,
Transmission-, Akteur- und Sender-ID, Scope, Membership-Version,
Empfängeranzahl, Zeitpunkt, Korrelations-ID sowie einen typisierten Ablehnungs-
oder Abbruchgrund. Die interne Empfängerliste ist ausdrücklich kein Bestandteil
des Events.

`SqliteControlPlaneRepository` implementiert den Audit-Sink synchron. Persistierte
Events können ab einer exklusiven Sequenz in stabiler Einfügereihenfolge und mit
einem Ergebnislimit gelesen werden. `eraseAuditEventsBefore` entfernt ältere
Events in begrenzten Batches; die konkrete Frist und der Scheduler bleiben
Betriebskonfiguration. Da die Anwendungsschnittstelle `record` bewusst
`noexcept` ist, zählt `droppedAuditEventCount` fehlgeschlagene Schreibvorgänge
für die spätere Betriebsüberwachung.

Die Schnittstelle ist der Anwendungskern. Der HTTP-Adapter gibt nach außen nur
geeignete Metadaten wie Transmission-ID, Scope, Status und Empfängeranzahl aus,
nicht die interne Empfängerliste.

## HTTP-v1-Adapter

`ControlPlaneHttpAdapter` stellt Session-Erstellung, Abfrage der eigenen
Membership, kurzlebige Voice-Grants sowie Start, Ende und Moderationsabbruch
von Transmissionen unter `/api/v1` bereit. Die vollständige Feld- und
Fehlerdefinition steht in `network-contract-v1.md`.

Die Authentifizierungsgrenze ist explizit: Nur die Session-Erstellung akzeptiert
ein externes Bearer-Credential und übergibt es an `ISessionAuthenticator`.
Geschützte Endpunkte akzeptieren ausschließlich das Schema `Session` mit der
zuvor ausgestellten Session-ID und prüfen Gerätebindung sowie Ablaufzeitpunkt.

Der Linux-Entry-Point verdrahtet den Adapter mit SQLite, dem
`InMemoryControlPlaneStore`, Rate Limits und Audit-Persistenz. Persistierte
Sessions werden beim ersten Anwendungszugriff direkt aus SQLite gelesen; die
atomare Aktivierungsprüfung verwendet dieselbe Quelle. Ein
Bootstrap-Identity-Provider liest das Credential aus einer Datei. Die
allgemeine `IdentitySessionAuthenticator`-Schicht stellt daraus 15 Minuten
gültige Sessions für genau einen konfigurierten Spieler aus. Ein produktiver
OIDC-, OAuth- oder eigener Account-Provider kann den Bootstrap-Provider
ersetzen, ohne HTTP-Adapter, Session-Persistenz oder Anwendungsfälle zu ändern.

IAM-01 legt als Nachfolger einen lokalen persistenten Accountadapter hinter
derselben `IIdentityProvider`-Grenze fest. Account, öffentliches Profil,
Membership und Rollen bleiben getrennt. Der Adapter verwendet
libsodium/Argon2id für Passwörter, gehashte Einmal- und Sessionsecrets,
rotierende gerätegebundene Erneuerungssecrets sowie einen einmaligen
Offline-Bootstrap. Die vollständige Entscheidung einschließlich
Bedrohungsmodell, Migration und Recovery steht in
[`identity-and-account-lifecycle.md`](identity-and-account-lifecycle.md).
Bis IAM-02 ist dies Zielarchitektur und kein bereits implementierter
Laufzeitpfad.

Der Linux-Socketadapter begrenzt Header und Body, lehnt Transfer-Encoding ab und
schließt jede Verbindung nach genau einem HTTP/1.1-Request. TLS bleibt Aufgabe
eines vorgeschalteten Reverse Proxys.

Die optionale Voice-Grant-Ausstellung verbindet
`VoiceGrantAuthorizationService` mit dem LiveKit-Tokenadapter. Server-URL und
API-Key werden als Startkonfiguration übergeben; das API-Secret wird
ausschließlich aus einer Datei gelesen. Ohne vollständige LiveKit-Konfiguration
bleiben die übrigen Control-Plane-Endpunkte verfügbar, während
`POST /api/v1/voice-grants` explizit `503` liefert.

## Server-Laufzeit

Der Entry-Point unterstützt einen dateibasierten Mehrbenutzer-Identity-Adapter
mit Geräte-Whitelist. Administrative Membership- und Moderationsrechte werden
aus der jeweils aktuellen autoritativen Rollenbelegung abgeleitet.

LiveKit-Grants beginnen ohne Publikationsrecht. Der atomare
Transmissionslebenszyklus erteilt und entzieht das Recht über RoomService,
während der Store gleichzeitig die konfigurierten Sprecherlimits pro
Hierarchieknoten durchsetzt. Timeout-, Session- und Audit-Retention-Jobs laufen
im Serverprozess.

Der Linux-Listener verarbeitet Requests mit einem begrenzten Worker-Pool,
Überlastschutz und geordnetem Signal-Shutdown. Readiness, Prometheus-Metriken,
strukturierte Betriebsereignisse sowie Docker-/Compose-Auslieferung sind in
[`server-runtime.md`](server-runtime.md) beschrieben.
