# Linux-Control-Plane

**Stand:** 25. Juli 2026

## Modulgrenzen

Die Control-Plane besteht aus zwei Targets:

- `hvc-application` enthält transport- und frameworkunabhängige
  Anwendungs-Schnittstellen, die serverseitige Autorisierung sowie vollständige
  Start- und End-Anwendungsfälle für Transmissionen.
- `hvc-control-plane` ist der ausführbare Server-Entry-Point. Netzwerkprotokoll,
  dauerhafte Persistenz und Transportadapter werden in folgenden Schritten
  ergänzt.

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
Persistenz und Cache-Invalidierung bleiben Adapteraufgaben. Der
`InMemoryControlPlaneStore` aktualisiert einen Kontext nur mit einer höheren
Version und beendet eine aktive Transmission desselben Spielers innerhalb
desselben kritischen Abschnitts.

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

Die Schnittstelle ist der Anwendungskern. Ein späterer HTTP-Adapter darf nach
außen nur geeignete Metadaten wie Transmission-ID, Scope, Status und
Empfängeranzahl ausgeben, nicht die interne Empfängerliste.

## Noch nicht enthalten

- HTTP- oder gRPC-Endpunkte
- dauerhafte Account-, Session- oder Gerätepersistenz
- kryptografische Tokenprüfung und kurzlebige Voice-Grants
- Rate Limits, Moderation und Audit-Log-Adapter
- LiveKit-Anbindung
