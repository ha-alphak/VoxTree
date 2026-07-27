# API-01: Directory-, Identity- und Verwaltungsverträge

**Abschlussdatum:** 27. Juli 2026  
**Status:** Abgenommen  
**Nachfolger:** DIR-01

## Abgeschlossener Umfang

API-01 hat die additiven HTTP-v1-Verträge vor der Laufzeitimplementierung
verbindlich festgelegt:

- aus der eigenen Group abgeleitetes Directory ohne frei wählbare Group-ID;
- getrennte, zusammengeführte Transport-Presence ohne Last-Seen- oder
  Gerätedaten;
- Passwortanmeldung, Aktivierung, Refreshrotation und kurzlebige
  Reauthentifizierungsnachweise;
- eigene Account-, Profil-, Geräte- und Sessionverwaltung;
- paginierte Account-, Einladungs-, Hierarchie-, Membership-, Rollen-,
  Moderations- und Auditressourcen;
- serverberechnete effektive Rechte und eine konfliktgesicherte Vorschau;
- starke ETags, `If-Match`, strikt steigende Versionen und an Akteur, Filter
  und Snapshot gebundene Cursor;
- generische öffentliche Authentifizierungsfehler und durchgängige
  Secret-Redaktion;
- atomare Auditpflicht für Verwaltungsänderungen.

Die vorhandenen Voice-v1-Verträge bleiben kompatibel. Insbesondere werden das
opake Bearer-Credential von `POST /api/v1/sessions`, das bestehende
Membership-Adminformat und der leere Transmission-Interrupt nicht
uminterpretiert. Persistente Clients und neue Moderationsoberflächen verwenden
getrennte neue Pfade.

## Verbindliche Dokumente

- [Directory-, Identity- und Verwaltungsvertrag v1](../network-contract-v1-api-01.md)
- [API-01-Vertragstests](../api-01-contract-tests.md)
- [Bestehender Control-Plane-Netzwerkvertrag v1](../network-contract-v1.md)
- [Identitäts- und Account-Lebenszyklus](../identity-and-account-lifecycle.md)
- [Architekturentscheidungen](../architecture-decisions.md)

## Abnahme gegen den Umsetzungsplan

| Kriterium | Nachweis |
|---|---|
| Positiv-, Negativ-, Datenschutz- und Autorisierungstests spezifiziert | Benannte Testfälle `API-COMP-*` bis `API-AUD-*` |
| Keine fremden Groups | Directory besitzt keinen Group-Parameter; `API-DIR-02/03` und `API-PRE-05` |
| Keine Verwaltungsmetadaten für Teilnehmer | serverseitige Rollenmatrix und `API-AZ-*`/`API-PRIV-*` |
| Keine Account-Enumeration | einheitlicher Fehlervertrag und `API-AUTH-02/03/09` |
| Paging und Konflikte | opake Snapshot-Cursor, ETags und `API-PAGE-*`/`API-VER-*` |
| Bestehende Voice-Verträge kompatibel | ausdrückliche Migrationsgrenze und `API-COMP-01` bis `API-COMP-06` |

## Ausgeführte Prüfungen

| Prüfung | Ergebnis |
|---|---|
| Lokale Markdown-Linkziele | bestanden; alle referenzierten Dateien vorhanden |
| `git diff --check` | bestanden |
| Windows MSVC Debug Build und CTest | bestanden; 15 von 15 Tests |
| Windows MSVC Release Build und CTest | bestanden; 15 von 15 Tests |
| Bestehender HTTP-Vertragstest `network.control_plane_http` | Debug und Release bestanden |
| Normative Sicherheitsquellen aus IAM-01 | NIST SP 800-63B-4, libsodium, OWASP Password/Session Management und RFC 9700 gegen den Vertrag abgeglichen |

Clang-Format, Clang-Tidy, Doxygen, reale LiveKit-, Hardware- und UI-Tests sind
für den reinen Vertrags-Freeze ohne C++-, ABI-, Medien- oder
Oberflächenänderung nicht einschlägig. Die neue Testmatrix wird in den jeweils
genannten Implementierungspaketen ausführbar gemacht; API-01 behauptet keine
noch nicht vorhandene Laufzeitfunktion.

## Verbleibende Arbeit

DIR-01 implementiert als nächstes Directory und Presence einschließlich der
zugehörigen `API-DIR-*`- und `API-PRE-*`-Vertragstests. Persistente
Authentifizierung und Accountverwaltung folgen getrennt in IAM-02/IAM-03,
Verwaltung und Moderation in ADM-01/MOD-01.
