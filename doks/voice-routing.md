# Voice-Routing

**Stand:** 26. Juli 2026

## Sicherheitsmodell

Die erste sichere Ausbaustufe verwendet ausschließlich getrennte LiveKit-Räume.
Ein Client erhält je nach seiner autoritativen Membership höchstens je einen
Grant für:

- `team:<team-id>`,
- `specialization:<specialization-id>`,
- `group:<group-id>`.

Raumnamen sind keine Clientberechtigung. Verbindlich ist ausschließlich das
serverseitig signierte, kurzlebige JWT mit genau einem festgelegten Raum.
`canSubscribe` und die Berechtigung, später `canPublish` zu erhalten, werden
unabhängig aus Rollenrichtlinie, Membership, Voice-Ban, Transmit-Mute und
Empfangsstatus abgeleitet. Das Token startet immer mit `canPublish=false`; erst
eine aktive serverautorisierte Transmission schaltet den exakten Raum über
RoomService frei. Ein reines
Senderecht bleibt deshalb auch bei gesperrtem Empfang nutzbar; ein reines
Empfangsrecht erteilt kein Publikationsrecht.

Ein Shared-Room-Modell und clientgesteuerte Selective Subscription sind nicht
Bestandteil dieser Stufe. Sie bleiben bis zu einem gesonderten Security- und
Lasttest zurückgestellt.

## PTT-Autorisierung

Der Start einer Transmission akzeptiert nur Session, Geräte-ID,
Client-Transmission-ID, Scope, Membership-Version und Korrelations-ID. Die
Control Plane:

1. leitet den Sender aus der gerätegebundenen Session ab,
2. lädt Membership und Rollenrichtlinie aus der autoritativen Quelle,
3. prüft Verbindung, aktive Gruppe, Scope, Rolle, Voice-Ban, Transmit-Mute und
   Membership-Version,
4. ermittelt Empfänger ausschließlich aus dem serverseitigen Snapshot,
5. aktiviert höchstens eine Transmission pro Spieler atomar.

Der Client publiziert das Mikrofon erst nach der positiven, korrelierten
Startantwort. Schlägt die lokale Publikation fehl, beendet er die bereits
aktivierte Servertransmission als Rollback.

## Abbruch aktiver Transmissionen

`InMemoryControlPlaneStore` koppelt die autoritative Änderung und den Abbruch in
einem kritischen Abschnitt. Aktive Transmissionen enden mit einem typisierten
Grund:

| Ereignis | Stop-Grund |
|---|---|
| Session-/Verbindungsabbau | `disconnected` |
| maximale Dauer erreicht | `timed_out` |
| Team-, Specialization- oder Group-Wechsel | `membership_changed` |
| Rollen- oder Berechtigungsänderung | `permission_revoked` |

Jeder erzwungene Abbruch erzeugt ein korreliertes Audit-Event ohne
Empfänger-IDs. Grant-Tokens sind kurzlebig und an Membership-Version sowie
Geräte-ID gebunden; neue Grants werden nur aus dem aktuellen Snapshot
ausgestellt. Der produktive Lifecycle-Hook entzieht das Publikationsrecht für
alle Abbruchgründe. Der native Security-Nachweis bestätigt zusätzlich, dass LiveKit
ein serverseitig entzogenes Publikationsrecht und den aktiven Track unmittelbar
entfernt.

## Automatisierte Nachweise

Die reguläre CTest-Suite deckt die Routingstufe ohne LiveKit-SDK-Abhängigkeit ab:

- `domain.routing` prüft Empfängerregeln, Team-, Specialization- und
  Cross-Group-Isolation sowie ein deterministisches 200-Spieler-Szenario.
- `application.control_plane` prüft serverseitige PTT-Autorisierung, veraltete
  Membership-Versionen, unzulässige Scopes und unabhängige Sende-/Empfangsrechte.
- `application.in_memory_control_plane` prüft atomare Abbrüche bei Disconnect,
  Timeout, Membership- und Rechteänderung.
- `livekit.token` prüft Least-Privilege-Claims und verschiedene Raumgrenzen für
  Cross-Team- und Cross-Group-Mitgliedschaften.

Das zusätzliche native Skript
`scripts/Invoke-LiveKitSecurityQualityGate.ps1` prüft gegen einen echten
LiveKit-Server den sofortigen Publish-Rechteentzug, verweigerte Subscriptions
und Cross-Room-Isolation. Tokens und API-Secret werden dabei nicht in
Testergebnisse geschrieben.
