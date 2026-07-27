# KDE-00: Technisches Debian-/KDE-Quality-Gate

**Stand:** 27. Juli 2026  
**Status:** Abgeschlossen

## Ergebnis und verbindliche Entscheidungen

KDE-00 bestätigt die gewählte Linux-Grundlage:

- Oberfläche: **Qt Widgets 6**. Der Prototyp startet nativ unter Plasma/Wayland,
  bindet Übersetzungen über Qt Linguist ein und hält Plattformtypen aus den
  gemeinsamen Präsentationsmodellen heraus.
- Voice-SDK: offizielles **LiveKit C++ SDK 1.4.0** für Linux x64, als
  SHA-256-geprüftes Archiv reproduzierbar eingebunden.
- Audio: direkter **PipeWire-Backendadapter** für Aufnahme, Wiedergabe,
  Geräteauflistung und Geräteauswahl. Die gemeinsame Raum-, Grant-,
  Publication- und Subscriptionlogik bleibt plattformneutral; Windows nutzt
  weiterhin XAudio2.
- Globale Tastatur-PTT-Aktionen: ausschließlich
  `org.freedesktop.portal.GlobalShortcuts` mit expliziter Desktopfreigabe.
- Gamepad/Joystick/HOTAS: unprivilegierter `libudev`-/evdev-Adapter über die
  vom Anmeldesystem vergebenen ACLs; kein Root-Dienst und kein allgemeiner
  Tastatur-Keylogger.
- Erstes Auslieferungsformat: versioniertes **Debian-Paket**; ein signiertes
  APT-Repository bleibt das Release-Gate des späteren Packaging-Pakets.

## Testumgebung

| Bestandteil | Nachweis |
|---|---|
| Betriebssystem | Debian 13 x64 |
| Desktop | KDE Plasma, Wayland-Sitzung |
| Qt | 6.8.2 |
| PipeWire | 1.4.2 |
| Compiler | GCC sowie Clang/clang-tidy 19 |
| LiveKit | C++ SDK 1.4.0, LiveKit Server 1.13.4 |
| Audiohardware | ein PipeWire-Aufnahme- und ein Wiedergabegerät |
| Eingabehardware | keine Gamepad-/Joystick-/HOTAS-Hardware am Testsystem |

Der lokale LiveKit-Testserver verwendete getrennte Team-, Specialization- und
Group-Räume. Tokens waren kurzlebig, wurden nicht protokolliert und gehören
nicht zu den Nachweisartefakten.

## Implementierter Quality-Gate-Umfang

### LiveKit und PipeWire

`LiveKitVoiceTransport` besitzt nun getrennte Audio-Backendgrenzen.
`PipeWireAudioBackend`:

- beobachtet Nodes und Standardgeräte über den PipeWire-Registry-/Metadata-Weg;
- nimmt mono PCM S16LE mit 48 kHz auf und übergibt Frames an eine
  LiveKit-`AudioSource`;
- liest dekodierte Remote-`AudioStream`-Frames und gibt sie über einen
  PipeWire-Stream aus;
- verwendet in den Echtzeitcallbacks vorallozierte, begrenzte Queues ohne
  Sperren oder Heap-Allokationen;
- beendet aktive Mikrofonpublikationen vor einem Aufnahmegerätewechsel;
- erzeugt nach einem Wiedergabegerätewechsel die Playoutstrecke neu, ohne eine
  Transmission fortzusetzen.

Die Subscription-Verarbeitung ist unabhängig davon, ob LiveKit zuerst
`onTrackSubscribed` oder `onTrackPublished` meldet. Die Linux-SDK-Callbacks
können Publication-Metadaten wie MIME-Typ, Quelle und Kind erst später
vervollständigen. In den serverseitig autorisierten Voice-Räumen werden daher
unbekannte und Audio-Tracks verarbeitet, eindeutig als Video erkannte Tracks
bleiben ausgeschlossen; die AudioStream-Erzeugung validiert den Medientyp.
LiveKit C++ 1.4.0 liefert bei `auto_subscribe=false` außerdem kein
`TrackPublished`-Ereignis. Der Transport verwendet deshalb das vom SDK
unterstützte Auto-Subscribe und wendet die HVC-Admission im ersten
Track-Callback an. Nicht zugelassene Tracks erhalten keinen
`RemoteAudioPlayout` und werden unmittelbar wieder abbestellt.

### Audioverarbeitung

WebRTC Audio Processing aktiviert High-Pass-Filter, Noise Suppression und AGC.
AEC bleibt im ersten PipeWire-Backend bewusst deaktiviert: Ohne einen
zeitlich ausgerichteten Reverse-Render-Stream würde ein aktiviertes AEC eine
nicht erfüllte Qualitätszusage vortäuschen. Die spätere Produktintegration
muss den gemischten Wiedergabestream mit gemessener Verzögerung einspeisen und
AEC danach mit Lautsprecher-/Mikrofon-Hardware qualifizieren.

### Wayland und Eingabe

Der Qt-Prototyp:

- zeigt Wayland-, Portal-, Aufnahme-, Wiedergabe- und HID-Fähigkeiten sichtbar;
- registriert drei globale PTT-Aktionen über das XDG-Portal;
- unterscheidet Press und Release und protokolliert, ob die Anwendung im
  Hintergrund war;
- verarbeitet die in KDE beobachteten D-Bus-Darstellungen des
  `session_handle` (`QDBusObjectPath`, D-Bus-Variant und `QString`);
- protokolliert Capability-, Portal- und normalisierte HID-Ereignisse ohne
  Credentials oder Voice-Inhalte.

Der Linux-Eingabeadapter filtert auf `ID_INPUT_JOYSTICK=1`, ordnet evdev-Buttons
stabil der HID-Button-Usage-Page zu und verarbeitet Gerätezuwachs und -entfernung
über udev. Automatisierte Lebenszyklus-, Normalisierungs- und Fehlerfälle sind
abgedeckt. Ein physischer Hot-Plug-Nachweis war mangels entsprechender Hardware
auf dem Testsystem nicht möglich und bleibt ein Hardware-Release-Gate; der
Prototyp zeigt das Fehlen des Geräts statt die Fähigkeit vorzutäuschen.

## Automatisierte Nachweise

| Gate | Ergebnis |
|---|---|
| GCC Debug, ASan/UBSan | 17 von 17 Tests bestanden |
| GCC Release | 17 von 17 Tests bestanden |
| Clang 19 und clang-tidy | 17 von 17 Tests bestanden |
| Doxygen | ohne Warnungen gebaut |
| CMake-Installation | erfolgreich |
| Windows MSVC Debug | 15 von 15 Tests bestanden |
| Windows LiveKit-Quality-Gate | erfolgreich gebaut |

Der KDE-Start-Smoke meldete Speicherlecks in QtDBus sowie
Mesa/Gallium/libva. Die Leak-Stacks enthielten keine HVC-Funktion. Die
HVC-eigenen Headless-Tests liefen unter ASan/LSan ohne Befund; der manuelle
Desktoplauf verwendet deshalb eine Release-Binärdatei und der externe
Treiberbefund ist separat dokumentiert.

## Manuelle Nachweismatrix

| Probe | Erwartung | Stand |
|---|---|---|
| KDE/Wayland-Start | Portal v1 und je mindestens ein Aufnahme-/Wiedergabegerät sichtbar | bestanden |
| Drei-Scope-Sender | reales Mikrofon wird in Team, Specialization und Group veröffentlicht und wieder entfernt | bestanden |
| Drei-Scope-Empfänger | selektiver Start und Stopp in allen drei Scopes | bestanden |
| Globales Portal-PTT | Press/Release bei Fremdfokus nach Desktopfreigabe | bestanden; alle drei Scopes |
| Kurze PTT-Impulse | 25 Press/Release-Zyklen je Scope, danach kein aktiver Track | bestanden; maximale Zykluslatenz 317 ms |
| Geräteauflistung/-wechsel | aktive Aufnahme vor Wechsel stoppen; Playout kontrolliert neu verbinden | Invariant implementiert und statisch geprüft; nur je ein physisches Gerät vorhanden |
| Serverneustart/Reconnect | Wiederverbindung ohne automatische Wiederaufnahme | bestanden |
| Rechteentzug | Veröffentlichung wird abgewiesen, Raum bleibt sauber verbunden | bestanden |
| HID-Hot-Plug | normalisierte Press/Release-Ereignisse | automatisiert bestanden; physische Hardware nicht vorhanden |

Der Abschlusslauf protokollierte für jeden Scope genau einen
`remote audio started`-/`remote audio stopped`-Zyklus und endete mit dem
produktiven Transport-PASS. Der Serverneustart erzeugte mehrere fehlgeschlagene
Resume-/Restart-Versuche, danach eine erfolgreiche Wiederverbindung ohne
lokale Publikation.

## Reproduktion

Die normalen Build-Gates verwenden die vorhandenen Presets:

```bash
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug --output-on-failure

cmake --preset linux-kde-quality-gate
cmake --build --preset linux-kde-quality-gate
ctest --preset linux-kde-quality-gate --output-on-failure

cmake --preset linux-clang-analysis
cmake --build --preset linux-clang-analysis
ctest --preset linux-clang-analysis --output-on-failure

cmake --preset linux-documentation
cmake --build --preset linux-documentation
```

Der native Zwei-Prozess-Lauf erfolgt mit
`hvc-livekit-quality-gate --transport-scope-cycle` auf Senderseite und
`--expect-audio` auf Empfängerseite. Kurzlebige Grants werden zur Laufzeit
bereitgestellt und dürfen weder in Befehlsbeispielen noch in Logs erscheinen.
