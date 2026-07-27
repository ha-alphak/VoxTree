# DIR-01: Serververzeichnis und Presence

**Abschlussdatum:** 27. Juli 2026  
**Status:** Abgenommen  
**Nachfolger:** DIR-02

## Abgeschlossener Umfang

DIR-01 implementiert den mit API-01 eingefrorenen Directory- und
Presence-Vertrag serverseitig:

- gruppenbegrenzte Hierarchie Group → Specialization → Team;
- höchstens 200 sichtbare Teilnehmer mit stabiler `PlayerId`, Anzeigename,
  Primär-Team und ausschließlich explizit freigegebenen Rollen;
- gruppenlokale Directory-Versionen, starke ETags und leere
  `304 Not Modified`-Antworten;
- eine von Membership getrennte Transport-Presence-Schnittstelle;
- zusammengeführte Online-/Offline-Zustände ohne Scope-, Geräte- oder
  Verlaufsdaten;
- vollständige Presence-Snapshots und begrenzte Deltas mit der neuesten
  Änderung je Spieler;
- Runtime-Verdrahtung im Control-Plane-Entry-Point;
- Anwendungs-, HTTP- und Linux-Listener-Regressionen für `API-DIR-*` und
  `API-PRE-*`.

## Architektur- und Datenschutzgrenzen

Der `DirectoryApplicationService` hängt nur von der autoritativen
Membership-Grenze und `ITransportPresenceProvider` ab. HTTP, SQLite, LiveKit
und spätere Accountpersistenz bleiben Adapter. Die sichtbare Group wird stets
aus dem authentifizierten Spieler abgeleitet; eine vom Client angegebene
Group-ID existiert nicht.

Directory-Antworten enthalten keine Account-ID, Anmeldenamen, Credentials,
Geräte, Sessions, IP-Adressen, Auditdaten, internen Rollenrichtlinien oder
effektiven Rechte. Presence enthält ausschließlich `player_id` und den
zusammengeführten Zustand `online`/`offline`. Änderungen einer fremden Group
beeinflussen weder Einträge noch die im Prozess geführte Version der eigenen
Sicht.

## Betrieb und bekannte Grenze

Der bestehende dateibasierte Identity-Modus besitzt noch keine persistenten
öffentlichen Profile oder einen administrierbaren öffentlichen Rollenkatalog.
Bis IAM-02 verwendet die Runtime deshalb die stabile `PlayerId` als
Anzeigename und veröffentlicht keine Rolle ohne explizite Freigabeprojektion.
Der neue Dienst unterstützt bereits versionierte Profil- und
Rollenkatalogprojektionen, damit IAM-02 und ADM-01 sie ohne Änderung des
Directory-Vertrags einspeisen können.

Die aktuelle Runtime-Presence stammt aus dem autoritativen Membership-Feld
`connected`, das eine verbundene Voice-Session repräsentiert. Die getrennte
Schnittstelle bildet intern eine Scope-Anzahl ab und kann später durch echte
LiveKit-Connect-/Disconnect-Ereignisse ersetzt werden. Die clientseitige
Zusammenführung von Transportereignissen mit Directory-Daten ist ausdrücklich
Gegenstand von DIR-02.

## Ausgeführte Prüfungen

| Prüfung | Ergebnis |
|---|---|
| Windows MSVC Debug Build und CTest | bestanden; 16 von 16 Tests |
| Windows MSVC Release Build und CTest | bestanden; 16 von 16 Tests |
| Debian GCC Debug mit AddressSanitizer und UndefinedBehaviorSanitizer | bestanden; 18 von 18 Tests |
| Debian GCC Release Build und CTest | bestanden; 18 von 18 Tests |
| Debian Clang 19 und projektweites Clang-Tidy | bestanden; 18 von 18 Tests |
| Linux-Sockettest für `304 Not Modified` | bestanden; korrekte Statuszeile, leerer Body und `Content-Length: 0` |
| Doxygen mit Warnungen als Fehler | bestanden |
| Projektweite Clang-Format-Trockenprüfung | bestanden |
| Lokale Markdown-Linkziele | bestanden; 33 Dokumente geprüft |
| `git diff --check` und Credential-Mustersuche in geänderten Dateien | bestanden |

`application.directory` deckt Gruppenisolation, sichtbare Versionen,
Rollenfilter, Teilnehmerlimit, Presence-Aggregation, Deltas und Retention ab.
`network.control_plane_http` deckt Datenminimierung, Query-Abwehr,
`ETag`/`304`, Snapshot-/Delta-Formen und Fehlercodes ab.

## Verbleibende Arbeit

DIR-02 reicht Remote-Participant-Connect/-Disconnect aus dem Clienttransport
weiter, führt die drei Voice-Scopes pro Spieler zusammen und verknüpft diese
Ereignisse deterministisch mit Directory- und Presence-Versionen. UX-02 stellt
den Kanalbaum und die Teilnehmer anschließend in den Desktop-Clients dar.
