# Projektdokumentation

Dieser Ordner enthält die versionierte Dokumentation für **Hierarchical Voice
Communication**.

## Dokumente

| Dokument | Inhalt |
|---|---|
| [spec.md](spec.md) | Ursprüngliche Software Feature Specification |
| [implementation-plan.md](implementation-plan.md) | Abgestimmter Umsetzungsplan |
| [architecture-decisions.md](architecture-decisions.md) | Aktuell verbindliche Architekturentscheidungen |
| [state-machines.md](state-machines.md) | Verbindungs- und Transmission-Zustandsautomaten |
| [control-plane.md](control-plane.md) | Anwendungsschicht und Control-Plane-Schnittstellen |
| [network-contract-v1.md](network-contract-v1.md) | Versionierter HTTP-Vertrag der Control-Plane |
| [livekit-quality-gate.md](livekit-quality-gate.md) | Native SDK-Anbindung und Zwei-Client-Probe |
| [voice-client.md](voice-client.md) | UI-unabhängiger Client-Core und LiveKit-Transportadapter |
| [client-core-integration.md](client-core-integration.md) | Versionierte DLL-, C-ABI- und C++-Integrationsschnittstelle |
| [voice-routing.md](voice-routing.md) | Getrennte Voice-Räume, Grants, PTT-Autorisierung und Isolation |
| [audio-engine.md](audio-engine.md) | Stream-Admission, Ducking, lokale Empfangsregeln und native Lautstärke |
| [input-system.md](input-system.md) | Windows-Raw-Input, PTT-Aktionen und Bindings |
| [windows-client.md](windows-client.md) | WinUI-Clientschale, Anmeldung und Bereitschaftszustand |
| [development.md](development.md) | Entwicklungsumgebung, Build und Qualitätsregeln |
| [CodeDocumentation.md](CodeDocumentation.md) | Verbindliche Regeln für C++-API-Dokumentation und Code-Kommentare |
| [status.md](status.md) | Laufender Projektstatus |

## Ablageregel

Spezifikationen, Pläne, Architekturentscheidungen, technische Dokumentationen
und Statusberichte werden ausschließlich in diesem Ordner abgelegt und mit Git
versioniert.
