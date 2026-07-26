# Code-Dokumentation

**Stand:** 26. Juli 2026

Dieses Dokument definiert die verbindlichen Regeln für Kommentare und
C++-API-Dokumentation in **Hierarchical Voice Communication**. Grundlage ist
der LSST-Leitfaden
[Documenting C++ Code](https://developer.lsst.io/cpp/api-docs.html). Die Regeln
wurden auf die Projektstruktur, die verwendeten C++20-Schnittstellen und den
aktuellen Entwicklungsworkflow angepasst.

## Ziele

Code-Dokumentation beschreibt den Vertrag einer Schnittstelle so, dass sie ohne
Kenntnis ihrer Implementierung korrekt verwendet werden kann. Interne Kommentare
erklären dagegen Entscheidungen, Invarianten und nicht offensichtliche
Implementierungsdetails.

Für dieses Projekt gelten deshalb zwei getrennte Kommentararten:

1. **Doxygen-Dokumentation** für alle öffentlichen oder geschützten
   C++-Bestandteile, insbesondere Namespaces, Typen, Funktionen, Methoden,
   Konstanten und Datenmember.
2. **Interne C++-Kommentare** mit `//` oder `/* ... */` für Informationen, die
   ausschließlich bei Wartung und Änderung der Implementierung benötigt werden.

Kommentare wiederholen nicht den unmittelbar lesbaren Code. Sie dokumentieren
den Zweck, den Vertrag oder den Grund einer Entscheidung und werden bei jeder
Verhaltensänderung mitgeändert.

## Sprache

- API-Dokumentation und Code-Kommentare werden auf Englisch geschrieben.
- Bezeichner werden mit Backticks markiert, zum Beispiel `session_id`.
- Projekt- und Benutzerdokumentation unter `doks/` darf auf Deutsch geschrieben
  werden.
- Formulierungen sind knapp, eindeutig und unabhängig von der aktuellen
  Implementierung.

## Geltungsbereich

Die Doxygen-Regeln gelten für die öffentlichen Header unter
`libs/*/include/` sowie für öffentliche oder geschützte Schnittstellen in
weiteren Headern. Private Bestandteile werden nur dokumentiert, wenn ihr Vertrag
oder ihre Invariante nicht aus dem Code hervorgeht.

Quelldateien unter `src/` enthalten normalerweise keine wiederholte
API-Dokumentation. Dort stehen bei Bedarf interne Kommentare zu Algorithmen,
Nebenläufigkeit, Ressourcenbesitz, Sicherheitsentscheidungen oder
plattformabhängigem Verhalten.

## Position und Format

### Dokumentation an der ersten Deklaration

Ein Dokumentationsblock steht dort, wo ein Bestandteil erstmals deklariert
wird. Bei einer in einem Header deklarierten und in einer `.cpp`-Datei
definierten Funktion steht die Dokumentation ausschließlich im Header.

Der Block steht unmittelbar vor der Deklaration und verwendet dieselbe
Einrückung:

```cpp
/**
 * Resolve the recipients authorized for a transmission.
 *
 * @param snapshot Authoritative membership state used for the decision.
 * @param sender_id Player requesting the transmission.
 * @param scope Requested communication scope.
 * @returns Players that may receive the transmission.
 */
[[nodiscard]] auto resolveRecipients(const MembershipSnapshot& snapshot,
                                     const PlayerId& sender_id,
                                     VoiceScope scope)
    -> std::vector<PlayerId>;
```

### Zulässige Begrenzer

Mehrzeilige Blöcke beginnen mit `/**` und enden mit `*/`. Beide Begrenzer stehen
auf eigenen Zeilen. Einzeilige Blöcke beginnen mit `///`.

```cpp
/// Identify the scope addressed by a voice transmission.
enum class VoiceScope : std::uint8_t
{
    /// Address members of the sender's current team.
    team,
    /// Address all teams in the sender's specialization.
    specialization,
    /// Address all eligible members of the sender's group.
    group
};
```

Die Formen `/*!` und `//!` werden nicht verwendet. `///<` ist nur für sehr kurze
Beschreibungen einzelner Enum-Werte, Konstanten oder Datenmember zulässig.
Parameter werden in neuem Code nicht mit nachgestellten `///<`-Kommentaren
dokumentiert.

### Tags und Formatierung

- Tags verwenden die Javadoc-Schreibweise mit `@`, beispielsweise `@param`,
  `@returns` und `@see`. Die gleichbedeutenden Schreibweisen mit Backslash
  werden nicht verwendet.
- Innerhalb von Doxygen-Blöcken wird bevorzugt Markdown verwendet.
- HTML oder spezielle Doxygen-Formatierung wird nur eingesetzt, wenn Markdown
  den Inhalt nicht ausdrücken kann.
- Dateibezogene `@file`-Blöcke werden nicht verwendet. Sie dokumentieren selten
  einen konkreten API-Vertrag und erschweren die Pflege.
- Die LSST-spezifischen Emacs- und Lizenzkopfzeilen werden nicht übernommen.
  Dieses Projekt ergänzt einen einheitlichen Dateikopf erst, wenn eine
  Projektlizenz festgelegt wurde.

## Aufbau eines Dokumentationsblocks

Abschnitte stehen, soweit sie zutreffen, in dieser Reihenfolge:

1. Kurzbeschreibung
2. Erweiterte Beschreibung
3. Template-Parameter mit `@tparam`
4. Funktionsparameter mit `@param`
5. Rückgabewert mit `@returns`
6. Ausnahmen mit `@throws`
7. Ausnahmesicherheit mit `@exceptsafe`
8. Zugehörige freie Funktionen mit `@relatesalso`
9. Initializer-Darstellung mit `@showinitializer` oder `@hideinitializer`
10. Verweise mit `@see`
11. Hinweise oder Warnungen mit `@note` beziehungsweise `@warning`
12. Literaturverweise mit `@cite`
13. Beispiele

Nicht benötigte Abschnitte werden weggelassen. Zwischen logisch getrennten
Abschnitten steht eine Leerzeile.

### Kurzbeschreibung

Die erste Zeile fasst den Vertrag in einem kurzen vollständigen Satz zusammen.
Sie nennt weder den Funktionsnamen noch eine bloße Wiederholung der Signatur.
Funktionen und Methoden werden möglichst im Imperativ beschrieben:

```cpp
/// Stop the active push-to-talk transmission.
[[nodiscard]] auto releasePushToTalk() -> VoiceTransportResult;
```

Eine mehrzeilige Kurzbeschreibung beginnt mit `@brief` und endet an der nächsten
Leerzeile. Das sollte nur ausnahmsweise nötig sein.

### Erweiterte Beschreibung

Ein kurzer Absatz erläutert Rolle und Umfang der API. Er umfasst in der Regel
höchstens drei Sätze. Detaillierte Nutzungsmuster, Hintergrundwissen und
Implementierungsgrenzen gehören in `@note`-Abschnitte oder Beispiele.

### Template-Parameter

Jeder Template-Parameter wird mit `@tparam` in der Reihenfolge der Signatur
beschrieben. Standardwerte müssen nicht wiederholt werden.

```cpp
/**
 * Store an identifier with its validated textual representation.
 *
 * @tparam Tag Type that distinguishes otherwise identical identifier kinds.
 */
template <typename Tag>
class StrongId;
```

Mehrzeilige Fortsetzungen werden eingerückt. Längere Erläuterungen gehören in
die erweiterte Beschreibung oder einen Hinweis, da Doxygen mehrteilige
Parameterbeschreibungen nur unzuverlässig verarbeitet.

### Funktionsparameter

Jeder Parameter wird mit `@param` in der Reihenfolge der Signatur beschrieben.
Die Beschreibung nennt Bedeutung, gültige Werte, Einheiten, Lebensdauer- oder
Besitzanforderungen und relevante Sonderfälle, nicht erneut den C++-Typ.

`[in]`, `[out]` und `[in, out]` werden angegeben, sobald eine Funktion mindestens
einen Ausgabeparameter besitzt:

```cpp
/**
 * Read the current audio levels.
 *
 * @param[out] peak Highest level observed since the previous read.
 * @param[out] average Average level observed since the previous read.
 */
void readAudioLevels(float& peak, float& average);
```

Werden Ein- und Ausgaben als Rückgabewert, Ergebnisstruktur oder `std::optional`
modelliert, ist diese Darstellung gegenüber neuen Ausgabeparametern zu
bevorzugen.

### Rückgabewerte

Nicht-triviale Rückgabewerte werden mit `@returns` beschrieben. Die Beschreibung
erklärt auch leere Werte, Fehlerzustände und bei Maps die Bedeutung von
Schlüssel und Wert.

Im Projekt wird einheitlich `@returns` verwendet, nicht `@return`.

### Ausnahmen und Ausnahmesicherheit

Jede absichtlich sichtbare Ausnahme wird mit einem namespace-qualifizierten
`@throws`-Eintrag dokumentiert. Der Eintrag erklärt die genaue Bedingung.
`@throw` und `@exception` werden nicht verwendet.

Die Ausnahmesicherheit kann mit `@exceptsafe` beschrieben werden:

- **no-throw:** Die Operation wirft keine Ausnahme.
- **strong:** Bei einer Ausnahme bleibt der beobachtbare Zustand unverändert.
- **basic:** Der Zustand bleibt gültig und Ressourcen werden nicht verloren.
- **none:** Es wird keine Ausnahmesicherheitsgarantie gegeben.

Eine als `noexcept` deklarierte Operation benötigt nur dann einen
`@exceptsafe`-Eintrag, wenn die Garantie oder ein mögliches Programmende durch
Abhängigkeiten erklärt werden muss.

### Hinweise, Warnungen und Verweise

`@note` beschreibt Nutzungsmuster, fachlichen Hintergrund oder für Aufrufer
relevante Implementierungsgrenzen. `@warning` kennzeichnet Bedingungen, deren
Missachtung zu Sicherheitsproblemen, Datenverlust oder ungültigem Zustand
führen kann.

Reine Implementierungsdetails bleiben interne Kommentare. Verweise auf
verwandte APIs oder Webseiten verwenden `@see`; Webseiten werden als
Markdown-Link angegeben. `@sa`, `@remark` und `@remarks` werden nicht verwendet.

## Typbezogene Regeln

### Namespaces

Jeder öffentliche Projekt-Namespace erhält genau eine kurze Beschreibung. Die
Dokumentation wird an einer zentralen Stelle gepflegt und nicht in jedem Header
wiederholt.

### Klassen, Structs und Typaliase

Der Block vor einer Klasse oder einem Struct beschreibt Verantwortung,
Invarianten, Besitzverhältnisse und Thread-Safety, soweit diese für Aufrufer
relevant sind. Einzelne Member erhalten zusätzlich ihre eigenen
Vertragsbeschreibungen.

Öffentliche Typaliase und `using`-Deklarationen werden mindestens mit einer
Kurzbeschreibung versehen, wenn ihre fachliche Bedeutung nicht eindeutig ist.

```cpp
/**
 * Coordinate client state transitions for one voice transport.
 *
 * The instance serializes observer access and owns no transport. The referenced
 * transport must outlive the client.
 *
 * @note Calls that mutate the connection or transmission state are not
 *     reentrant.
 */
class VoiceClient final;
```

### Enumerationen

Die Enumeration selbst und jeder Wert werden dokumentiert. Kurze
Wertbeschreibungen stehen direkt davor oder ausnahmsweise als `///<` dahinter.
Die Beschreibung erläutert die fachliche Bedeutung, nicht den Namen.

### Funktionen und Methoden

Alle öffentlichen oder geschützten Funktionen und Methoden erhalten einen
Dokumentationsblock. Dieser beschreibt mindestens die Kurzfassung sowie alle
Parameter und nicht-trivialen Rückgabewerte. Relevante Vorbedingungen,
Nachbedingungen, Seiteneffekte, Thread-Safety und Fehlerzustände werden
ergänzt.

Bei virtuellen Schnittstellen steht der vollständige Vertrag an der
Basismethode. Überschreibungen dokumentieren nur Abweichungen oder zusätzliche
Garantien; ansonsten verwenden sie `@copydoc`.

`@overload` darf nur verwendet werden, wenn der vollständige Vertrag eindeutig
aus einer vollständig dokumentierten Überladung hervorgeht. Die erzeugte
Dokumentation muss geprüft werden, weil Doxygen Überladungen sortiert.

Freie Operatoren oder Hilfsfunktionen, die fachlich zu einem Typ gehören, dürfen
sparsam mit `@relatesalso TypeName` zugeordnet werden.

### Konstanten, Variablen und Datenmember

Alle nicht privaten Konstanten, Variablen und Datenmember erhalten mindestens
eine Kurzbeschreibung. Bei Konstanten können `@showinitializer` oder
`@hideinitializer` festlegen, ob der Initialwert Teil des öffentlichen Vertrags
ist.

Öffentliche Datenmember sollten nur verwendet werden, wenn der Typ bewusst ein
einfacher Datenträger ist. Ihre Dokumentation beschreibt Bedeutung, Einheit und
Gültigkeitsbereich.

## Interne Kommentare

Interne Kommentare beantworten bevorzugt **warum** etwas geschieht. Geeignete
Inhalte sind:

- Sicherheits- und Autorisierungsentscheidungen;
- atomare Invarianten und Sperrreihenfolgen;
- Besitz, Lebensdauer und plattformspezifische Ressourcenregeln;
- Gründe für ungewöhnliche Algorithmen oder bewusst verworfene Alternativen;
- Protokollanforderungen und Kompatibilitätsgrenzen;
- gezielte Unterdrückungen von Compiler- oder Analysewarnungen.

Beispiel:

```cpp
// Compare and replace the membership snapshot while holding the same lock so a
// transmission can never become visible with a stale authorization decision.
```

Ungeeignet ist eine bloße Übersetzung des Codes:

```cpp
// Increment the retry count.
++retry_count;
```

TODO-Kommentare enthalten einen nachvollziehbaren Arbeitsbezug, wenn ein
Issue-Tracker eingeführt wurde. Auskommentierter Code wird gelöscht und bleibt
über die Versionsverwaltung auffindbar.

## Projektworkflow

### Bei einer Codeänderung

1. Vor der Implementierung wird festgestellt, ob ein öffentlicher oder
   geschützter Vertrag geändert wird.
2. Die Doxygen-Dokumentation wird zusammen mit der ersten Deklaration geändert.
3. Interne Kommentare werden auf weiterhin gültige Annahmen geprüft.
4. Neue Parameter, Rückgabewerte, Ausnahmen und Enum-Werte werden vollständig
   dokumentiert.
5. Beispiele und `@see`-Verweise werden angepasst, wenn sich die Verwendung
   ändert.
6. Formatierung, Build, Tests, statische Analyse und Dokumentationsprüfung
   werden vor dem Review ausgeführt.

### Lokale Prüfung

Unter Debian wird die API-Dokumentation mit dem CMake-Workflow erzeugt:

```bash
apt-get install doxygen
./scripts/build.sh linux-documentation
```

Die HTML-Ausgabe liegt anschließend unter
`out/build/linux-documentation/documentation/html/`.

Die Doxygen-Konfiguration behandelt fehlerhafte Tags, nicht auflösbare
Verweise, falsch benannte Parameter und andere Dokumentationsfehler als
Buildfehler. Der bestehende undokumentierte Altbestand wird schrittweise
überarbeitet; bis diese Migration abgeschlossen ist, wird fehlende
Dokumentation zusätzlich im Review geprüft und noch nicht allein durch Doxygen
zum Buildfehler gemacht.

### Review-Checkliste

- Ist jeder neue oder geänderte öffentliche beziehungsweise geschützte Vertrag
  dokumentiert?
- Beschreibt die Dokumentation Verhalten und Randfälle statt die Signatur zu
  wiederholen?
- Stimmen Parameterreihenfolge, Namen, Rückgabewerte und Ausnahmen mit dem Code
  überein?
- Sind Sicherheits-, Thread-Safety- und Lebensdauergarantien ausdrücklich
  festgehalten?
- Steht API-Dokumentation nur an der ersten Deklaration?
- Erklären interne Kommentare weiterhin den tatsächlichen Grund der
  Implementierung?
- Werden Code, Tests und Dokumentation in derselben Änderung geliefert?

## Referenzen

- [LSST DM Developer Guide: Documenting C++ Code](https://developer.lsst.io/cpp/api-docs.html)
- [Doxygen Manual](https://www.doxygen.nl/manual/)

