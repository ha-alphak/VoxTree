# Linux-Control-Plane

**Stand:** 25. Juli 2026

## Modulgrenzen

Das Grundgerüst besteht aus zwei neuen Targets:

- `hvc-application` enthält transport- und frameworkunabhängige
  Anwendungs-Schnittstellen sowie die serverseitige Autorisierung für den Start
  einer Transmission.
- `hvc-control-plane` ist der ausführbare Server-Entry-Point. Netzwerkprotokoll,
  Persistenz und konkrete Adapter werden in folgenden Schritten ergänzt.

Die Anwendungsschicht verwendet `hvc-domain` für die autoritative
Empfängerermittlung. Sie hängt weder von LiveKit noch von einem HTTP-Framework
oder einer Datenbank ab.

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

## Autoritative Membership

`IAuthoritativeMembershipProvider` liefert für einen authentifizierten Spieler
einen konsistenten Anwendungskontext aus:

- immutablem, versioniertem `MembershipSnapshot`
- dazugehöriger `RolePolicy`

Beide Objekte werden zusammen bezogen, damit die Transmission nicht gegen eine
Membership-Version und eine davon abweichende Rollenrichtlinie geprüft wird.
Persistenz, Cache-Invalidierung und atomare Aktualisierung sind Adapteraufgaben.

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

Die Schnittstelle ist der Anwendungskern. Ein späterer HTTP-Adapter darf nach
außen nur geeignete Metadaten wie Transmission-ID, Scope, Status und
Empfängeranzahl ausgeben, nicht die interne Empfängerliste.

## Noch nicht enthalten

- HTTP- oder gRPC-Endpunkte
- konkrete Account-, Session- oder Gerätepersistenz
- kryptografische Tokenprüfung und kurzlebige Voice-Grants
- Registry und Beendigung aktiver Transmissionen
- Rate Limits, Moderation und Audit-Log-Adapter
- LiveKit-Anbindung
