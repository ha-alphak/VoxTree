# Debian-Client für KDE Plasma

**Stand:** 26. Juli 2026<br>
**Status:** Verbindlicher Vor-Release-Umfang, noch nicht implementiert

## Ziel

Neben dem WinUI-Client entsteht ein nativer Desktop-Client für Debian 13 x64
mit KDE Plasma. Wayland ist der primäre Sitzungsweg; eine vorhandene
Plasma-X11-Sitzung kann ergänzend getestet werden, definiert aber nicht die
Sicherheitsarchitektur.

Der Debian-Client ist kein unabhängiges Produkt und keine reduzierte
Nebenoberfläche. Er verwendet dieselben:

- Control-Plane- und Directory-Verträge;
- Account-, Membership-, Rollen- und Moderationsregeln;
- PTT-, Audio-Policy- und Zustandsmodelle;
- Diagnoseereignisse und Datenschutzregeln;
- deutschen und englischen Begriffe sowie Abnahmekriterien.

## Wiederverwendbare Grundlage

Bereits plattformneutral sind:

- `hvc-domain`;
- `hvc-client` einschließlich Autorisierung, PTT-Koordination,
  Audio-Admission, Ducking und Teilnehmerregeln;
- `hvc-client-core` und seine versionierte C-ABI;
- der HTTP-/JSON-Vertrag;
- die geplanten Präsentationsmodelle.

Noch Windows-spezifisch sind:

- WinUI 3 und die aktuelle `ClientSession`-Zusammenstellung;
- WinHTTP;
- Raw Input und Windows-HID-Auswertung;
- XAudio2-Wiedergabe im LiveKit-Transport;
- Win32-Ressourcen, Credentialdialog und lokale Pfade;
- Build, Laufzeitabhängigkeiten und Paketierung des nativen LiveKit-SDK.

## Technisches Zielbild

| Bereich | Debian/KDE-Ziel |
|---|---|
| Oberfläche | C++20 mit Qt 6; Qt Widgets oder Qt Quick/Kirigami wird durch KDE-00 entschieden |
| Desktop | KDE Plasma, Wayland primär |
| HTTP | Linux-Adapter hinter `IClientHttpTransport`, TLS-Prüfung über System-Truststore |
| Voice | gemeinsamer LiveKit-Vertrag mit plattformneutralem Raum-/Publikationskern |
| Audio | PipeWire für Aufnahme, Wiedergabe, Geräteauflistung und Hot-Plugging |
| Tastatur-PTT | XDG Desktop Portal `GlobalShortcuts`, einschließlich Press/Release |
| HID/PTT | unprivilegierter Linux-Adapter für Gamepad, Joystick und HOTAS |
| Einstellungen | XDG Base Directory, atomare versionierte Konfiguration |
| Geheimnisse | Secret Service/KWallet; keine Klartext-Credentials in Konfigurationsdateien |
| Diagnose | gemeinsames Ereignisschema, Logs unter XDG State/Cache und redigierter Support-Bundle |
| Paketierung | versioniertes Debian-Paket mit signierten Repository-/Release-Metadaten |

## Wayland- und Eingabegrenzen

Wayland verhindert absichtlich das allgemeine globale Abgreifen von Tastatur-
und Mauseingaben. Der Client umgeht diese Grenze nicht über Root-Rechte,
systemweite Hooks oder versteckte `/dev/input`-Zugriffe.

Für Team-, Specialization- und Group-PTT werden globale Tastaturaktionen über
das XDG-Global-Shortcuts-Portal registriert. Der Desktop zeigt dabei den
Freigabe-/Konfigurationsdialog. Der Client wertet die Portalereignisse für
Aktivierung und Deaktivierung als PTT Press/Release aus.

Gamepads, Joysticks und HOTAS benötigen keinen Compositor-Keylogger. Ein
eigener Adapter verwendet eine etablierte HID-Bibliothek und Geräteerkennung,
meldet fehlende Benutzerberechtigungen sichtbar und liefert dieselben
normalisierten `InputEvent`-Werte wie Raw Input.

Zusätzliche globale Maustasten werden nur angeboten, wenn eine
unprivilegierte, vom Desktop ausdrücklich freigegebene Schnittstelle sie
zuverlässig bereitstellt. Andernfalls zeigt die Capability-Matrix diese
Bindingklasse als nicht verfügbar, statt die Sicherheitsgrenze zu umgehen.

## Audio- und LiveKit-Grenze

`LiveKitVoiceTransport` enthält derzeit Raumlogik und XAudio2-Wiedergabe in
einer Implementierung. Vor dem Debian-Client werden getrennt:

1. gemeinsamer Raum-, Grant-, Publication- und Subscription-Lebenszyklus;
2. Aufnahme-/Gerätevertrag;
3. Remote-Track-Playoutvertrag;
4. Windows-XAudio2-Backend;
5. Debian-PipeWire-Backend.

Das KDE-00-Quality-Gate prüft insbesondere, ob die gepinnte
LiveKit-C++-Version unter Debian Aufnahme und WebRTC-Audioverarbeitung in der
geforderten Qualität bereitstellt. Fehlende Funktionen werden im HVC-Backend
ergänzt oder führen vor dem UI-Ausbau zu einer dokumentierten
Transportentscheidung.

## Oberfläche

Die Qt-Oberfläche bildet dieselbe Informationsarchitektur wie WinUI ab:

- Hauptfenster mit Kanalbaum und Teilnehmern;
- separates Einstellungsfenster;
- separates Diagnosefenster;
- rollenabhängige Account-, Rechte- und Moderationsverwaltung.

Qt- und WinUI-Code teilen keine Widgettypen. Beide konsumieren dieselben
UI-unabhängigen Präsentationsmodelle. Dadurch werden Directory-Zuordnung,
Presence-Zusammenführung, Rechteanzeige, Einstellungsvalidierung und
Fehlerabbildung nur einmal fachlich implementiert und getestet.

## Qualität und Auslieferung

Pflichtnachweise:

- Debian-13-Clean-Build mit GCC und Clang;
- KDE-Plasma-/Wayland-Start-Smoke;
- echte Mikrofonaufnahme und Wiedergabe;
- globale Portal-PTT-Aktionen bei Fremdfokus;
- HID-/HOTAS-Hot-Plugging auf verfügbarer Testhardware;
- Gerätewechsel, Reconnect und Serverneustart;
- kein automatisches Fortsetzen einer Transmission;
- Windows↔Debian-Interoperabilität in beiden Richtungen;
- Kanal-, Teilnehmer-, Account-, Rechte- und Diagnoseparität;
- Tastaturbedienung, Screenreader-Semantik, hoher Kontrast und Skalierung;
- Installation, Upgrade und Deinstallation des Debian-Pakets sowie Prüfung der
  signierten Repository-/Release-Metadaten.

## Entscheidungsgrundlagen

- [XDG Desktop Portal: Global Shortcuts](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html)
- [PipeWire API](https://docs.pipewire.org/page_api.html)
- [PipeWire-Audiomodell](https://docs.pipewire.org/1.6/page_audio.html)
- [KDE-Entwicklungsdokumentation für Qt 6](https://develop.kde.org/docs/getting-started/building/kde-builder-qt6/)
