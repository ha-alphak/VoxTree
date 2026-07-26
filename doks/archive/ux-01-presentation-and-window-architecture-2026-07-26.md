# UX-01: Präsentations- und Fensterarchitektur

**Abgeschlossen:** 26. Juli 2026  
**Arbeitspaket:** UX-01  
**Ergebnis:** Abgenommen

## Umfang

UX-01 trennt die bisher in `apps/windows-client/src/main.cpp` gebündelte
WinUI-Oberfläche in klar verantwortete Fensterschalen und führt eine
plattformneutrale Präsentationsgrenze ein. Fachlicher Funktionsausbau wie
Directory/Presence, persistente Einstellungen, produktive Administration und
Diagnoseexport bleibt in den nachfolgenden Arbeitspaketen.

## Umsetzung

- `main.cpp` enthält nur noch den Prozesseinstieg; `App` startet die
  Anwendungsschale.
- `MainWindow` besitzt die aktive `ClientSession` und koordiniert
  Verbindungs-, Membership-, PTT- und Sprecherzustände.
- `SettingsWindow` und `DiagnosticsWindow` sind eigenständige, nicht-modale
  Fenster ohne Besitz an Netzwerk- oder Voice-Diensten.
- `hvc::presentation` definiert UI-unabhängige Zustände, Befehle,
  Validierungen und stabile Fehlercodes für Verbindung, Kanalwahl, Teilnehmer,
  Einstellungen, Administration und Diagnose.
- Rollenabhängige Verwaltungszustände verwenden exakte Rollen-IDs;
  Teilzeichenfolgen gewähren keine sichtbare Berechtigung.
- Session-Rückrufe werden mit schwachen Referenzen und einer
  Verbindungsgeneration auf den WinUI-Dispatcher übertragen. Veraltete
  Rückrufe werden verworfen.
- Netzwerk-, LiveKit-, Audio- und Einstellungsoperationen laufen außerhalb des
  UI-Threads. Häufige Teilnehmeränderungen werden 150 Millisekunden gebündelt
  und serialisiert.
- `ClientSession` schützt ihren Dienstgraphen und trennt Worker-Abbruch,
  Join und Teardown ohne gleichzeitigen ungeschützten Zugriff.
- Ein Build-Gate prüft, dass die englische und deutsche
  Win32-Stringtable dieselben Ressourcen-IDs enthalten.

Der verbindliche Zustands-, Besitz-, Lebensdauer- und Threadingvertrag ist in
[../presentation-architecture.md](../presentation-architecture.md)
dokumentiert.

## Abnahmekriterien

| Kriterium | Nachweis |
|---|---|
| Getrennte Verantwortlichkeiten für Haupt-, Einstellungs- und Diagnosefenster | Eigene `MainWindow`-, `SettingsWindow`- und `DiagnosticsWindow`-Implementierungen; schlanker Prozesseinstieg |
| Präsentationszustände ohne WinUI testbar | Eigenständige Bibliothek `hvc::presentation` und Test `presentation.desktop_model` |
| Keine WinUI- oder Qt-Typen in Präsentationszuständen | Öffentliche C++20-Schnittstelle verwendet ausschließlich Standard- und bestehende Clienttypen |
| Keine Netzwerk- oder LiveKit-Operation blockiert den UI-Thread | Hintergrund-Coroutinen, Dispatcher-Rückführung, serialisierte Session-Grenze und Generationstest der Zustandslogik |
| Englische/deutsche Ressourcenparität | `VerifyWin32StringTableParity.cmake` als WinUI-Pre-Build-Gate |

## Validierung

- Windows Debug: vollständiger Build und 15 von 15 CTest-Tests bestanden.
- Windows Release: vollständiger serieller Build und 15 von 15 CTest-Tests
  bestanden.
- WinUI Debug und Release: `hvc-windows-client.exe` erfolgreich gebaut;
  englische/deutsche Ressourcenparität bestanden.
- Vollständige WinUI-Debug-Konfiguration: alle Targets gebaut und 15 von 15
  Tests bestanden.
- `clang-tidy`: keine Warnung im geänderten Präsentationsmodell oder dessen
  Tests.
- `clang-format --dry-run --Werror`: alle geänderten C++-Dateien bestanden.
- Doxygen-Dokumentationsbuild: bestanden.
- Prozess-Smoke-Test: Debug-Client startete, blieb fünf Sekunden aktiv und
  wurde anschließend kontrolliert beendet.
- `git diff --check`: bestanden.

Die NuGet-Sicherheitsabfrage konnte in der eingeschränkten Buildumgebung den
öffentlichen Dienstindex nicht erreichen (`NU1900`). Paketwiederherstellung,
Build und Tests waren davon nicht betroffen.

## Bewusst verbleibender Umfang

- Ein verbundener visueller Zwei-Client- und Barrierefreiheitstest gehört zu
  den späteren Produkt- und Release-Gates.
- DIR-01 liefert erst den Kanalbaum und vollständige Teilnehmer-/Presence-Daten.
- SET-01 ergänzt Persistenz, Migration und vollständiges Binding-Lernen.
- ADM-01 und DIA-01 ergänzen produktive Verwaltung beziehungsweise
  Support-Bundle-Export.
- Der nächste offene Abschnitt des Plans ist KDE-00.
