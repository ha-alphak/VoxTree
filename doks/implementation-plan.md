# Umsetzungsplan vor der Auslieferungsreife

**Projekt:** Hierarchical Voice Communication<br>
**Stand:** 26. Juli 2026<br>
**Geltungsbereich:** Offene Arbeiten nach dem bestandenen Abschnitt 10.2<br>
**Ziel:** Produktreife Windows- und Debian/KDE-Client-, Verwaltungs- und
Diagnosepfade als Voraussetzung für Abschnitt 10.3

Der abgeschlossene ursprüngliche Plan ist unter
[archive/implementation-plan-through-10.2-2026-07-26.md](archive/implementation-plan-through-10.2-2026-07-26.md)
archiviert. Dieses Dokument enthält bewusst nur noch offene Arbeiten.

## Arbeitsregeln

- Jede Phase endet mit den angegebenen Abnahmekriterien.
- UI-Berechtigungen steuern nur die Sichtbarkeit; die Control Plane entscheidet
  weiterhin autoritativ.
- Neue Netzwerkverträge werden vor Clientcode dokumentiert, versioniert,
  negativ getestet und auf Datenminimierung geprüft.
- Kein Log, Diagnoseexport oder Audit-Event enthält Credentials, Session-IDs,
  Voice-Grant-Tokens oder Voice-Inhalte.
- Neue UI-Funktionen erhalten ein UI-unabhängiges Präsentationsmodell und
  automatisierte Tests. Plattformtests ergänzen diese Grenze.
- Windows und Debian/KDE verwenden dieselben fachlichen Präsentationsmodelle,
  Ereignisse und Netzwerkverträge. Plattformtypen bleiben in den jeweiligen
  Fensterschalen und Adaptern.
- Ein manueller Zwei-Client-Test ersetzt keine automatisierte Regression,
  sondern ist ein zusätzliches Release-Gate.

## Zielbild der Desktop-Clients

```text
Gemeinsamer Client-Core und Präsentationsmodelle
├── Windows-Shell: WinUI 3
└── Debian/KDE-Shell: Qt 6, Wayland primär
    └── identische Produktbereiche
        ├── Verbindung und eigene Identität
        ├── Kanalbaum: Group → Specialization → Team
        ├── Teilnehmer des gewählten Kanals
        │   ├── Anzeigename, Präsenz und sichtbare Rolle
        │   ├── Sprechzustand und Scope
        │   └── lokale Lautstärke, Mute und Block
        ├── PTT- und Verbindungsstatus
        ├── Einstellungen (eigenes Fenster)
        ├── Diagnose (eigenes Fenster)
        └── Verwaltung (rollenabhängiger Bereich)
```

Die Kanalansicht bildet serverdefinierte Scopes ab. Sie darf kein freies
Beitreten zu einem LiveKit-Raum anbieten und keine clientseitige
Empfängerauswahl erzeugen.

## Phase 0: Stabilisieren und Verträge festlegen

### PRE-01 – Kurze PTT-Impulse

1. Eine deterministische Probe für Press/Release während einer noch
   ausstehenden LiveKit-Publikation erstellen.
2. Publikationszustände mindestens als `idle`, `starting`, `active` und
   `stopping` modellieren.
3. Ein Release während `starting` als Abbruchwunsch behandeln und den
   serverseitigen Transmissionslebenszyklus genau einmal beenden.
4. Verspätete Publish-Ergebnisse durch eine Generation/Korrelation verwerfen
   und gegebenenfalls unmittelbar unpublizieren.
5. Sendestatus erst nach bestätigter lokaler Publikation anzeigen.
6. Schnelle Impulse, Disconnect, Rechteentzug und Gerätefehler als
   Regressionstests abdecken.

Abnahme:

- 100 schnelle PTT-Zyklen je Scope ohne `publish time out`, hängenden Track,
  doppeltes Ende oder verbleibendes Publish-Recht;
- normale PTT-Latenz und bestehende Sicherheitsgates bleiben bestanden;
- Fehler werden mit Scope, Zustandsübergang und Korrelations-ID diagnostizierbar
  protokolliert.

### UX-01 – Präsentations- und Fensterarchitektur

1. `main.cpp` in App-/Fensterkoordination, Präsentationsmodelle und
   wiederverwendbare Views aufteilen.
2. Plattformneutrale Präsentationsmodelle so abgrenzen, dass WinUI und Qt
   dieselben Zustände, Befehle, Validierungen und Fehlercodes verwenden.
3. Besitz, Lebensdauer und Threadwechsel von `ClientSession` und
   Dispatcher-Ereignissen dokumentieren.
4. Zustandsmodelle für Verbindung, Kanalwahl, Teilnehmer, Einstellungen,
   Administration und Diagnose definieren.
5. Deutsche und englische Ressourcenparität für beide Oberflächen als
   Build-Gate beibehalten.

Abnahme:

- Haupt-, Einstellungs- und Diagnosefenster besitzen getrennte
  Verantwortlichkeiten;
- Präsentationszustände sind ohne WinUI testbar;
- Präsentationszustände enthalten weder WinUI- noch Qt-Typen;
- keine Netzwerk- oder LiveKit-Operation blockiert den UI-Thread.

### KDE-00 – Technisches Debian-/KDE-Quality-Gate

Vor dem Bau der Qt-Oberfläche:

1. Das gepinnte native LiveKit-C++-SDK reproduzierbar für Debian 13 x64 bauen
   oder eine kompatible Linux-Distribution des SDK festlegen.
2. Verbindung zu Team-, Specialization- und Group-Räumen, Opus-Publikation,
   selektiven Empfang und Unpublish mit zwei nativen Linux-Prozessen
   nachweisen.
3. Den Windows-spezifischen XAudio2-Teil hinter eine Audio-Backend-Grenze
   verschieben und Aufnahme/Wiedergabe über PipeWire prüfen.
4. Geräteauflistung, Default-Gerätewechsel, Hot-Plugging, AEC,
   Noise Suppression und AGC bewerten.
5. Press/Release für globale Tastatur-PTT-Aktionen unter KDE/Wayland über das
   XDG-Global-Shortcuts-Portal nachweisen.
6. Gamepad-, Joystick- und HOTAS-Buttons über einen unprivilegierten
   Linux-HID-Adapter samt Hot-Plugging prüfen.
7. Reconnect, Serverneustart, Rechteentzug und kurze PTT-Impulse mit denselben
   Sicherheitsinvarianten wie unter Windows testen.
8. Einen kleinen Qt-6-/KDE-Prototyp einschließlich CMake- und
   Lokalisierungsintegration bauen.

Abnahme:

- zwei Debian/KDE-Prozesse senden und empfangen echtes Mikrofon-Audio in allen
  autorisierten Scopes;
- mindestens ein globales PTT-Binding funktioniert bei Fremdfokus unter
  Wayland mit expliziter Desktopfreigabe;
- Gerätewechsel und Reconnect setzen keine Transmission automatisch fort;
- fehlende Portal- oder HID-Fähigkeiten werden als Capability erkannt und
  verständlich angezeigt;
- das Ergebnis entscheidet verbindlich über Qt Widgets oder Qt Quick/Kirigami,
  Linux-Audioadapter und Paketformat.

### IAM-01 – Identität und Account-Lebenszyklus

Vor der Implementierung wird eine Architekturentscheidung mit folgendem
Zielmodell festgeschrieben:

- geschlossene, administrative Provisionierung statt offener
  Selbstregistrierung;
- initialer Administrator über einen einmaligen Bootstrap-Vorgang;
- Accountanlage mit einmaliger Einladung/Aktivierung;
- sichere Credential-Erstellung, -Änderung und -Rücksetzung;
- Account-Deaktivierung, Geräte- und Sitzungswiderruf;
- getrennte Speicherung von Account, öffentlichem Profil, Membership und
  Rollen;
- lokaler persistenter Identity-Adapter für selbst betriebenen Betrieb;
- weiterhin austauschbare `IIdentityProvider`-Grenze für einen späteren
  externen Provider;
- bewährte Passwort-Hashing-Bibliothek und parametrisierte Hashes, keine eigene
  Kryptografie.

Abnahme:

- Bedrohungsanalyse für Anmeldung, Aktivierung, Reset, Brute Force,
  Sessiondiebstahl und Administratorverlust;
- dokumentierter Migrationsweg von der Identity-Datei;
- festgelegte Recovery-Prozedur für den ersten Administrator.

### API-01 – Verzeichnis- und Verwaltungsverträge

Vor der UI werden versionierte Verträge für folgende Sichten festgelegt:

- sichtbare Hierarchie und Mitglieder des eigenen Groups;
- öffentliches Teilnehmerprofil und Presence-Zustand;
- paginierte Accountliste und Account-Lifecycle;
- Hierarchie-, Membership- und Rollenkatalog;
- effektive Sende-/Empfangsrechte;
- aktive Transmissionen für autorisierte Moderatoren;
- optimistic concurrency beziehungsweise strikt steigende Versionen;
- Audit-Datensätze für jede administrative Änderung.

Abnahme:

- Positiv-, Negativ-, Datenschutz- und Autorisierungstests sind spezifiziert;
- ein normaler Teilnehmer kann weder fremde Groups auflisten noch
  Verwaltungsmetadaten abrufen;
- die bestehenden v1-Voice-Verträge bleiben kompatibel oder erhalten einen
  dokumentierten Migrationspfad.

## Phase 1: Verzeichnis, Presence und neue Hauptansichten

### DIR-01 – Serververzeichnis

1. Eine datenschutzbegrenzte Directory-Anwendungsschnittstelle implementieren.
2. Die sichtbare Group-Hierarchie und Teilnehmer mit stabiler ID,
   Anzeigename, Primär-Team und freigegebenen Rollen liefern.
3. Änderungen versionieren und inkrementell oder durch begrenztes Polling
   aktualisieren.
4. Transportpräsenz getrennt von Membership modellieren.

### DIR-02 – Client-Präsenz

1. Remote-Participant-Connect/-Disconnect als strukturierte Clientereignisse
   bis ins Präsentationsmodell weiterreichen.
2. Mehrfachanwesenheit desselben Spielers in Team-, Specialization- und
   Group-Raum zu einem Teilnehmerzustand zusammenführen.
3. Presence, Audioverfügbarkeit und tatsächliches Sprechen getrennt anzeigen.
4. Reconnect, Membership-Wechsel und veraltete Ereignisse deterministisch
   behandeln.

### UX-02 – Kanal- und Teilnehmeransicht

1. Navigierbaren Baum Group → Specialization → Team darstellen.
2. Auswahl eines Knotens zeigt die daraus serverseitig abgeleiteten
   Teilnehmer; ein Teilnehmer wird im Strukturbaum primär unter seinem Team
   geführt.
3. Eigene Position, Sende-/Empfangsrecht und aktueller PTT-Scope klar anzeigen.
4. Teilnehmerzeilen zeigen Anzeigename, Onlinezustand, Sprechzustand, Scope und
   freigegebene Rolle.
5. Lokale Lautstärke, Mute und Block pro Teilnehmer bereitstellen.
6. Leere, ladende, getrennte, veraltete und nicht autorisierte Zustände
   explizit gestalten.

### KDE-01 – Qt-6-/KDE-Clientschale

1. Eigenes CMake-Ziel unter `apps/debian-client` mit gepinnten beziehungsweise
   distributionsgebundenen Qt-/KDE-Abhängigkeiten anlegen.
2. Einen Linux-HTTP-Adapter hinter `IClientHttpTransport` implementieren.
3. Gemeinsamen Client-Core, Directory-, Account- und Diagnosemodelle ohne
   fachliche Duplikation anbinden.
4. Haupt-, Einstellungs-, Diagnose- und Verwaltungsfenster gemäß demselben
   Informationsmodell wie WinUI umsetzen.
5. KDE-Systemthema, hohe Kontraste, Skalierung, Tastaturnavigation,
   Screenreader-Semantik und lokalisierte Texte unterstützen.
6. XDG-Verzeichnisse für Konfiguration, Cache, Status und Logs verwenden;
   speicherbare Geheimnisse ausschließlich über Secret Service/KWallet
   ablegen.
7. Tray-/Benachrichtigungsintegration nur über standardisierte
   Desktop-Schnittstellen implementieren.

Abnahme Phase 1:

- zwei reale Windows-Clients sowie zwei Debian/KDE-Clients erscheinen innerhalb
  des festgelegten Zielwerts im richtigen Kanal und verschwinden nach
  Disconnect/Reconnect korrekt;
- ein Windows- und ein Debian/KDE-Client bestehen den gemischten
  Sende-/Empfangstest in beiden Richtungen;
- Teilnehmer werden auch dann dargestellt, wenn sie gerade nicht sprechen;
- Scope- und Group-Isolation wird durch die neue Directory-API nicht
  abgeschwächt;
- Tastaturbedienung, Fokusreihenfolge, Skalierung und hoher Kontrast
  funktionieren unter WinUI und KDE Plasma.

## Phase 2: Eigenständiges Einstellungsfenster

### SET-01 – Einstellungen und Persistenz

1. Einstellungen aus den Hauptansichten entfernen und je Plattform in einem
   eigenen nicht-modalen Fenster öffnen.
2. Kategorien für Audio, Eingabe/PTT, Benachrichtigungen,
   Barrierefreiheit und Erweitert bereitstellen.
3. Aufnahme- und Wiedergabegeräte mit Test-/Pegelrückmeldung auswählen.
4. Alle Raw-Input-Klassen einschließlich HID-/HOTAS-Buttons über einen
   Lernmodus belegen.
5. Konflikte vor dem Übernehmen vollständig anzeigen.
6. Einstellungen pro Betriebssystembenutzer versioniert und atomar speichern;
   unter Debian gelten die XDG-Verzeichnisse. Credentials und Sessions gehören
   nicht in diese Datei.
7. Änderungen je nach Wirkung sofort oder über
   Übernehmen/Verwerfen/Rücksetzen anwenden.

Abnahme:

- keine Einstellung verbleibt als eingebetteter Block in der Hauptansicht;
- Neustart stellt gültige Einstellungen wieder her und migriert ältere
  Versionen;
- Geräteverlust, ungültige Konfiguration und fehlerhafter Audio-Reconnect
  hinterlassen einen konsistenten Zustand;
- PTT bleibt bei Fremdfokus funktionsfähig.

## Phase 3: Accounts, Rechte und Moderation

### IAM-02 – Persistenter Accountdienst

1. Account-, Credential-, Einladung-, Geräte- und Sessionmodelle mit
   SQLite-Migrationen implementieren.
2. Login, Aktivierung, Passwortänderung, Reset, Abmeldung aller Geräte und
   Sessionwiderruf mit Rate Limits bereitstellen.
3. Den statischen Identity-Dateimodus ausdrücklich auf Entwicklung und
   Migration begrenzen.
4. Initialen Administrator sicher bootstrappen und den Vorgang anschließend
   deaktivieren.
5. Sicherheitsrelevante Vorgänge auditieren, ohne Credentialmaterial zu
   speichern.

### IAM-03 – Anmeldung und Selbstverwaltung

1. Anmeldeansicht für Server, Accountname und Passwort/Aktivierungscode
   erstellen.
2. Accountaktivierung und erzwungenen Credentialwechsel unterstützen.
3. Eigenes Profil, bekannte Geräte und Sitzungen anzeigen.
4. Passwort ändern sowie einzelne oder alle anderen Sitzungen widerrufen.
5. Fehler wie deaktivierter Account, abgelaufene Einladung und Rate Limit
   verständlich, aber ohne Account-Enumeration anzeigen.

### ADM-01 – Membership- und Rechteverwaltung

1. Accountliste mit Suche, Paging und Statusfiltern implementieren.
2. Membership über Group, Specialization und Team bearbeiten.
3. Rollen zuweisen und effektive Rechte vor dem Speichern anzeigen.
4. Rollenrichtlinien als Matrix aus Team-/Specialization-/Group-Senden und
   getrenntem Empfangen bearbeiten.
5. Änderungen mit Versionskonflikt-Erkennung, Bestätigung und Auditbezug
   speichern.
6. Löschen, Deaktivieren und Membership-Entfernen als unterschiedliche
   Operationen darstellen.

### MOD-01 – Moderationsoberfläche

1. Aktive Transmissionen mit Sender, Scope, Startzeit und stabiler
   Transmission-ID für berechtigte Moderatoren auflisten.
2. Autorisierten Abbruch mit Grund und Bestätigung ermöglichen.
3. Optionales Transmit-Mute als getrennte Membership-/Rechteänderung
   darstellen.
4. Erfolg, bereits beendete Transmission und Rechteverlust konsistent
   behandeln.

Abnahme Phase 3:

- vollständiger Ablauf
  Administrator-Bootstrap → Einladung → Aktivierung → Anmeldung →
  Membership/Rolle → Deaktivierung ist Ende-zu-Ende getestet;
- ein Moderator kann Transmissionen beenden, aber keine Accounts oder
  Rollenrichtlinien ändern;
- ein normaler Teilnehmer kann Verwaltungsendpunkte weder sehen noch direkt
  verwenden;
- parallele Adminänderungen verlieren keine Daten und liefern erkennbare
  Versionskonflikte;
- alle Änderungen sind mit Akteur, Ziel, Zeitpunkt, Ergebnis und
  Korrelations-ID auditiert.

## Phase 4: Clientdiagnose und Betriebsfähigkeit

### DIA-01 – Strukturierte Clientprotokolle

1. Gemeinsames Ereignisschema mit UTC-Zeit, Schweregrad, Komponente,
   Ereigniscode, Scope, Zustandsübergang und Korrelations-ID definieren.
2. Rotierende lokale Dateien mit Größen- und Aufbewahrungsgrenzen schreiben.
3. Control Plane, LiveKit, Audio, Raw Input, Membership-Refresh und UI-Aktionen
   anbinden.
4. Geheimnisse bereits an der Erzeugungsgrenze redigieren.
5. Debug- und Release-Builds identisch diagnostizierbar machen.
6. Unter Debian die XDG-Verzeichnisse und eine optionale, explizite
   Journal-Anbindung beachten; Dateilogs bleiben ohne erhöhte Rechte nutzbar.

### DIA-02 – Diagnosefenster und Support-Bundle

Das Diagnosefenster zeigt:

- App-, ABI-, LiveKit- und Betriebssystemversion;
- Control-Plane-Erreichbarkeit und Sitzungsablauf ohne Session-ID;
- Membership-Version und Grants-Ablauf ohne Token;
- Zustand jedes Voice-Scope-Raums;
- Aufnahme-/Wiedergabegerät und Audiofehler;
- Raw-Input-Geräte, Ereigniszähler und letzte erkannte PTT-Aktion;
- letzte strukturierte Fehler mit Kopierfunktion.

Der Export enthält redigierte Logs, Konfiguration, Versionsinformationen und
eine kurze Zustandsaufnahme. Vor dem Schreiben wird eine Vorschau der
enthaltenen Kategorien angezeigt.

Abnahme:

- ein absichtlich ausgelöster Fehler ist über Client, Control Plane und
  LiveKit anhand einer Korrelations-ID nachvollziehbar;
- automatisierte Tests finden in einem Support-Bundle keine bekannten
  Credentials, Sessions oder Tokens;
- Dateirotation, parallele Ereignisse und ein nicht beschreibbarer Logpfad
  beeinträchtigen Voice und PTT nicht.

## Phase 5: Produktabnahme und Abschnitt 10.3

### QA-01 – Funktions- und UI-Abnahme

- Unit- und Vertragstests für neue Präsentations-, Directory-, Account- und
  Verwaltungsmodelle;
- WinUI-Smokes für Fensterlebenszyklus, Navigation und Dispatcher;
- Ende-zu-Ende-Matrix mit Teilnehmer, Moderator und Administrator;
- zwei reale Windows-Clients mit Mikrofon, Wiedergabe und Fremdfokus-PTT;
- zwei reale Debian/KDE-Clients unter Wayland mit Mikrofon, Wiedergabe und
  globalem PTT sowie ein gemischtes Windows-/Debian-Paar;
- Reconnect, Serverausfall, Membershipwechsel, Account-Deaktivierung,
  Sessionwiderruf und kurze PTT-Impulse;
- Screenreader, Tastatur allein, hoher Kontrast, 100–200 % Windows-Skalierung
  und große Textdarstellung;
- KDE-Skalierung, Systemthema, Screenreader-Semantik und Portal-Freigaben;
- Windows 10 22H2, eine aktuelle unterstützte Windows-11-Version und Debian 13
  mit KDE Plasma unter Wayland.

### REL-01 – Abschnitt 10.3 ausführen

Erst nach Abschluss aller vorherigen Gates:

1. Datenschutz-, Abhängigkeits- und Bedrohungsmodell finalisieren.
2. Datenschutz- und Aufbewahrungseinstellungen dokumentieren.
3. Signiertes MSIX mit Upgrade-/Rollback-Test erzeugen.
4. Versioniertes Debian-Clientpaket mit Installation, Upgrade, Deinstallation,
   Desktopdatei und Abhängigkeitsprüfung erzeugen; Repository beziehungsweise
   Release-Metadaten werden signiert. Flatpak folgt nur nach einer gesonderten
   Packaging-Entscheidung.
5. Unveränderliche, versionierte Linux-Servercontainer mit SBOM und
   Schwachstellenprüfung erzeugen.
6. Betriebs-, Backup-/Restore-, Account-, Konfigurations-, Moderations-,
   Diagnose-, Datenschutz- und Integrationsdokumentation abschließen.
7. Sauberen Checkout auf unterstützten Windows- und Debian-Systemen bauen,
   testen, installieren und deinstallieren.

## Definition of Done

Der Vor-Release-Plan ist abgeschlossen, wenn:

- alle Arbeitspakete PRE, API, DIR, UX, KDE, SET, IAM, ADM, MOD, DIA und QA
  ihre Abnahme erfüllen;
- keine release-blockierende bekannte Störung offen ist;
- alle Spezifikationskriterien umgesetzt oder mit Begründung und
  Risikoeigner zurückgestellt sind;
- ein unabhängiger Testlauf die neue Clientoberfläche und die
  Verwaltungsabläufe reproduziert;
- Installations-, Betriebs- und Supportpfade ohne Entwicklungsartefakte
  funktionieren;
- der aktuelle Status ausschließlich belegte Ergebnisse nennt und die
  detaillierten Nachweise verlinkt.
