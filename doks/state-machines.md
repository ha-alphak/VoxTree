# Zustandsautomaten

**Stand:** 26. Juli 2026

Die Zustandsautomaten sind Teil von `hvc-domain` und hängen weder vom gewählten
Voice-Transport noch von einer Benutzeroberfläche ab. Ungültige oder veraltete
Ereignisse werden verworfen und verändern den Zustand nicht.

## Verbindung

```text
Disconnected
  -> ConnectingTransport
  -> RefreshingMembership
  -> RestoringSubscriptions
  -> Ready
```

Ein Verbindungsverlust aus einem aktiven Verbindungszustand führt zu
`ReconnectingTransport`. Nach der erneuten Transportverbindung müssen die
autoritative Membership erneut geladen und die Empfangsabonnements erneut
angewendet werden, bevor `Ready` erreicht wird.

Eine Membership-Änderung ist nur mit einer höheren Version gültig. Sie führt
von `Ready` zurück nach `RestoringSubscriptions`.

## Transmission

```text
Idle
  -> Requesting
  -> Transmitting
  -> Ending
  -> Idle
```

- Die UI darf erst in `Transmitting` einen aktiven Sender anzeigen.
- Eine Ablehnung führt von `Requesting` nach `Idle`.
- Wird Push-to-Talk vor der Annahme losgelassen, endet die Anfrage sofort.
- Antworten werden mit einer `ClientTransmissionId` korreliert. Eine verspätete
  Antwort auf eine ältere Anfrage kann keine neuere Anfrage aktivieren.
- Verbindungsverlust, Membership-Änderung, Rechteentzug, Timeout und
  Transportfehler beenden sowohl angefragte als auch aktive Transmissionen
  unmittelbar.

## Reconnect-Invariante

Beim Verbindungsverlust werden Membership-Version und Transmission-Kontext
gelöscht. Der ausgewählte lokale Scope bleibt für die Bedienoberfläche erhalten,
erzeugt nach dem Reconnect aber keine neue Transmission. Dafür ist immer eine
neue explizite Push-to-Talk-Anfrage mit einer neuen `ClientTransmissionId`
erforderlich.

## Lokale Mikrofonpublikation

Die lokale Medienpublikation besitzt zusätzlich zum serverseitigen
Transmissionszustand einen eigenen, transportnahen Lebenszyklus:

```text
Idle
  -> Starting
  -> Active
  -> Stopping
  -> Idle
```

- Jede angenommene PTT-Anfrage erhält eine strikt steigende
  Publikationsgeneration.
- `Starting` endet erst, wenn LiveKit eine nicht leere Publication-SID liefert
  und dieselbe SID in der lokalen Publication-Map des verbundenen Raums
  sichtbar ist.
- Ein Release in `Starting` setzt einen Abbruchwunsch. Das Startergebnis darf
  dann nicht mehr nach `Active` wechseln; ein verspätet bestätigter Track wird
  sofort wieder unpubliziert.
- Der autorisierte Client beendet die zur Generation gehörende
  Control-Plane-Transmission genau einmal. Erst danach wird `Idle` erreicht.
- Disconnect, Reconnect, Membership- oder Rechteänderung verwenden denselben
  Abbruchpfad und nehmen die Publikation nie automatisch wieder auf.
- Fehlerdiagnosen nennen Scope, Zustandsübergang, Generation und die
  `ClientTransmissionId` als Korrelations-ID.
