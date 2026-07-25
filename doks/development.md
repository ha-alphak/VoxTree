# Entwicklungsumgebung

**Stand:** 25. Juli 2026

## Unterstützte Toolchains

| Plattform | Toolchain |
|---|---|
| Windows 10/11 x64 | Visual Studio 2022, MSVC/v143 |
| Debian 13 x64 | GCC und Ninja |
| Statische Analyse | Clang und clang-tidy |

Erforderlich ist CMake 3.25 oder neuer. Der Quellcode verwendet C++20 ohne
Compilererweiterungen.

## Windows

Benötigte Visual-Studio-Komponenten:

- Desktopentwicklung mit C++
- MSVC v143 x64/x86-Buildtools
- Windows SDK
- CMake-Tools für Windows

Debug-Build und Tests:

```powershell
.\scripts\build-windows.cmd -Preset windows-msvc-debug
```

Release-Build und Tests:

```powershell
.\scripts\build-windows.cmd -Preset windows-msvc-release
```

Der Wrapper startet das PowerShell-Buildskript unabhängig von der lokalen
PowerShell-Ausführungsrichtlinie. Das Skript verwendet CMake aus dem `PATH` oder
findet die mit Visual Studio installierte Version über `vswhere.exe`.

## Debian 13

Benötigte Pakete:

```bash
apt-get install build-essential cmake ninja-build
```

Debug-Build mit AddressSanitizer und UndefinedBehaviorSanitizer:

```bash
./scripts/build.sh linux-gcc-debug
```

Release-Build:

```bash
./scripts/build.sh linux-gcc-release
```

Statische Analyse:

```bash
apt-get install clang clang-tidy
./scripts/build.sh linux-clang-analysis
```

## Presets

`CMakePresets.json` enthält alle gemeinsamen und CI-relevanten Presets.
Persönliche Einstellungen dürfen in der nicht versionierten Datei
`CMakeUserPresets.json` ergänzt werden.

Buildausgaben werden ausschließlich unter `out/` erzeugt und nicht mit Git
versioniert.

## Qualitätsregeln

- Warnungen im eigenen Code werden als Fehler behandelt.
- MSVC baut mit `/W4`, standardkonformem Präprozessorverhalten und UTF-8.
- GCC und Clang bauen unter anderem mit `-Wall`, `-Wextra`, `-Wpedantic`,
  `-Wconversion` und `-Wshadow`.
- Linux-Debug-Builds verwenden AddressSanitizer und
  UndefinedBehaviorSanitizer.
- clang-tidy prüft Fehler, Nebenläufigkeit, Modern C++, Performance,
  Portabilität und Lesbarkeit.
- clang-format und `.editorconfig` definieren die Quellcodeformatierung.

## Abhängigkeiten

Das Projektfundament verwendet noch keine externen C++-Bibliotheken. Neue
Abhängigkeiten werden erst nach Lizenz-, Sicherheits- und Wartungsprüfung
aufgenommen und müssen auf eine reproduzierbare Version festgelegt werden.
Direkte Downloads einer unbestimmten neuesten Version sind in Build und CI
nicht zulässig.
