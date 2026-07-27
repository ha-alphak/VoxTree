# Projektdokumentation

Dieser Ordner enthält die versionierte Dokumentation für **Hierarchical Voice
Communication**.

## Dokumente

| Dokument | Inhalt |
|---|---|
| [spec.md](spec.md) | Ursprüngliche Software Feature Specification |
| [implementation-plan.md](implementation-plan.md) | Aktueller Plan der noch offenen Vor-Release-Arbeiten |
| [architecture-decisions.md](architecture-decisions.md) | Aktuell verbindliche Architekturentscheidungen |
| [state-machines.md](state-machines.md) | Verbindungs- und Transmission-Zustandsautomaten |
| [control-plane.md](control-plane.md) | Anwendungsschicht und Control-Plane-Schnittstellen |
| [network-contract-v1.md](network-contract-v1.md) | Versionierter HTTP-Vertrag der Control-Plane |
| [network-contract-v1-api-01.md](network-contract-v1-api-01.md) | Verbindliche v1-Verträge für Directory, Presence, Identity und Verwaltung |
| [api-01-contract-tests.md](api-01-contract-tests.md) | Positiv-, Negativ-, Datenschutz- und Autorisierungstests für API-01 |
| [identity-and-account-lifecycle.md](identity-and-account-lifecycle.md) | Verbindliches Identitäts-, Account-, Credential-, Session-, Bootstrap- und Migrationsmodell |
| [livekit-quality-gate.md](livekit-quality-gate.md) | Native SDK-Anbindung und Zwei-Client-Probe |
| [voice-client.md](voice-client.md) | UI-unabhängiger Client-Core und LiveKit-Transportadapter |
| [client-core-integration.md](client-core-integration.md) | Versionierte DLL-, C-ABI- und C++-Integrationsschnittstelle |
| [voice-routing.md](voice-routing.md) | Getrennte Voice-Räume, Grants, PTT-Autorisierung und Isolation |
| [audio-engine.md](audio-engine.md) | Stream-Admission, Ducking, lokale Empfangsregeln und native Lautstärke |
| [input-system.md](input-system.md) | Windows-Raw-Input, PTT-Aktionen und Bindings |
| [windows-client.md](windows-client.md) | WinUI-Clientschale, Anmeldung und Bereitschaftszustand |
| [presentation-architecture.md](presentation-architecture.md) | Gemeinsame Präsentationszustände, Fensterbesitz und Threading |
| [debian-kde-client.md](debian-kde-client.md) | Qualifizierte Grundlage des nativen Debian-Clients für KDE Plasma und Wayland |
| [kde-00-quality-gate.md](kde-00-quality-gate.md) | LiveKit-, PipeWire-, Wayland-, Eingabe- und Qt-6-Plattformnachweis |
| [server-runtime.md](server-runtime.md) | Mehrbenutzerbetrieb, LiveKit-Control, Jobs, HTTP und Container |
| [load-and-security-tests.md](load-and-security-tests.md) | Reproduzierbarer 200-Spieler-Last-, Ausfall- und Sicherheitsnachweis |
| [development.md](development.md) | Entwicklungsumgebung, Build und Qualitätsregeln |
| [CodeDocumentation.md](CodeDocumentation.md) | Verbindliche Regeln für C++-API-Dokumentation und Code-Kommentare |
| [status.md](status.md) | Laufender Projektstatus |
| [archive/README.md](archive/README.md) | Abgeschlossene und abgelöste Status- und Planungsstände |

## Ablageregel

Spezifikationen, Pläne, Architekturentscheidungen, technische Dokumentationen
und Statusberichte werden ausschließlich in diesem Ordner abgelegt und mit Git
versioniert.

`status.md` enthält nur den aktuellen belastbaren Stand und die aktiven
Release-Blocker. `implementation-plan.md` enthält nur offene Arbeitspakete.
Ausführliche abgeschlossene Listen werden im Unterordner `archive` bewahrt,
damit laufende Arbeit nicht durch historische Aufgaben überlagert wird.
