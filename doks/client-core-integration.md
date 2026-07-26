# Client-Core-Integrationsschnittstelle

**Stand:** 26. Juli 2026  
**ABI:** `1.0` (`0x00010000`)

## Zweck und Grenzen

`hvc-client-core` stellt den UI-unabhängigen Voice-Client als Shared Library
bereit. Ein Spiel oder eine andere Hostanwendung kann dadurch eine eigene
Oberfläche, Control-Plane-Anbindung und Voice-Transportimplementierung
verwenden, ohne C++-Standardbibliothekstypen oder interne HVC-Klassen über die
DLL-Grenze auszutauschen.

Die erste Version umfasst:

- Verbindung zu einem bis drei autorisierten Scope-Räumen;
- exklusiven Team-, Specialization- oder Group-PTT-Lebenszyklus;
- Stream-Admission und Ducking über den vorhandenen `VoiceClient`;
- vollständige, versionierte Membership-Ereignisse;
- Verbindungs-, Sprecher- und Fehlerereignisse;
- eine C-ABI und einen besitzenden C++-RAII-Wrapper.

Die DLL führt in ABI-Version 1 keine Anmeldung und keine
Transmission-Autorisierung selbst aus. Der Host verwendet dafür den vorhandenen
`ControlPlaneClient` oder einen vertragskompatiblen Adapter. Die Reihenfolge
„Serverautorisierung vor Mikrofonpublikation“ bleibt verbindlich.

Eine lokale IPC-Schnittstelle ist bewusst nicht Bestandteil von Version 1. Sie
ist nur für eine spätere Trennung in mehrere Prozesse vorgesehen; die aktuelle
In-Process-DLL vermeidet zusätzliche Authentifizierungs-, Rechte- und
Lebenszyklusgrenzen.

## Artefakte

| Bestandteil | Verwendung |
|---|---|
| `hvc-client-core.dll` | Windows-x64-Laufzeitbibliothek |
| `hvc-client-core.lib` | MSVC-Importbibliothek |
| `libhvc-client-core.so` | Linux-Shared-Library |
| `hvc/client_core.h` | Compilerneutrale C-ABI |
| `hvc/client_core.hpp` | Header-only C++20-Wrapper |
| CMake-Ziel `hvc::client_core` | Build- und Paketkonsument |

Die DLL wird zusammen mit Headern und CMake-Paketmetadaten durch den regulären
Installationsschritt ausgeliefert. Unter Windows muss `hvc-client-core.dll`
neben der Host-EXE oder in einem kontrollierten DLL-Suchpfad liegen.

## ABI-Kompatibilität

`HVC_CLIENT_CORE_ABI_VERSION` codiert die Major-Version in den oberen und die
Minor-Version in den unteren 16 Bit. Die Bibliothek akzeptiert
Konfigurationen derselben Major-Version bis zu der von ihr implementierten
Minor-Version. Ein Integrator prüft die tatsächlich geladene Bibliothek vor dem
Erzeugen eines Handles:

```c
if ((hvc_client_core_api_version() >> 16U) !=
    (HVC_CLIENT_CORE_ABI_VERSION >> 16U)) {
    /* Load a compatible DLL. */
}
```

Für den Vertrag gelten folgende Regeln:

- exportierte Funktionen verwenden `extern "C"` und unter Windows `__cdecl`;
- ein `hvc_client_core*` ist opak und wird ausschließlich durch
  `hvc_client_core_create()` und `hvc_client_core_destroy()` verwaltet;
- jede erweiterbare Struktur beginnt mit `struct_size`;
- neue Minor-Versionen dürfen Strukturen nur am Ende erweitern;
- Result-, Scope-, Zustands-, Event- und Fehlerwerte verwenden auf der
  Wire-Grenze explizit `uint32_t`; vorhandene symbolische Werte,
  Feldbedeutungen und Funktionssignaturen bleiben unverändert;
- weder Exceptions noch C++-Container, RTTI-Typen oder Allokatorbesitz
  überschreiten die DLL-Grenze;
- Text ist UTF-8 und wird als Zeiger plus Byteanzahl übergeben.

Eingabestrings werden innerhalb des synchronen Aufrufs kopiert. Zeiger in
Ereignissen sind nur bis zur Rückkehr aus dem Callback gültig. Der C++-Wrapper
wandelt jedes Ereignis vor dem Aufruf des Integrators in besitzende
`std::string`- und `std::vector`-Werte um.

## Host-Transport

`hvc_client_core_transport_v1` ist eine vom Host gefüllte Funktionstabelle. Sie
bildet die interne `IVoiceTransport`-Grenze ohne C++-Typen ab:

- Observer synchron an- und abmelden;
- aktuellen Verbindungszustand lesen;
- Scope-Grants verbinden und alle Räume trennen;
- genau eine Mikrofonpublikation starten oder stoppen;
- den aktiven Mikrofon-Scope abfragen;
- Admission und linearen Gain auf einen Remote-Stream anwenden.

Der Transport meldet asynchron Verbindung, verfügbare Remote-Spuren,
tatsächlich hörbare Sprecher und Fehler über
`hvc_client_core_transport_observer_v1`. Eine verfügbare Spur wird zunächst nur
als Kandidat gemeldet. Erst `configure_remote_audio(..., admitted != 0, gain)`
erlaubt Dekodierung und Wiedergabe.

Alle Funktionszeiger sind in Version 1 erforderlich. Der Host besitzt das in
`user_data` referenzierte Objekt und muss garantieren, dass es das
Client-Core-Handle überlebt. `set_observer(..., NULL, ...)` beendet
synchron alle weiteren Callbacks.

Audiogeräteauflistung und -wechsel bleiben in ABI-Version 1 beim konkreten
Transportadapter. Sie können in einer kompatiblen späteren Minor-Version als
zusätzliche, optionale Funktionen ergänzt werden.

## Ereignisvertrag

Jedes `hvc_client_core_event_v1` enthält `struct_size`, `abi_version` und eine
pro Handle strikt steigende `sequence`. Nicht zum `kind` gehörende Felder sind
leer oder null.

| Ereignis | Nutzdaten |
|---|---|
| `MEMBERSHIP_UPDATED` | vollständiges `hvc_client_core_membership_v1` |
| `MEMBERSHIP_CLEARED` | keine |
| `CONNECTION_STATE_CHANGED` | `connection_state` |
| `SPEAKER_STARTED` | `scope`, `participant_id` |
| `SPEAKER_STOPPED` | `scope`, `participant_id` |
| `ERROR` | `error_code`, `message` |

Memberships werden nur mit einer Version größer als die aktuell gespeicherte
Version akzeptiert. Ein Snapshot enthält Hierarchie-, Spieler-, Group-,
Specialization- und Team-ID, Rollen sowie getrennte Empfangs- und
Transmit-Mute-Zustände. `clear_membership()` hebt die Versionssperre für eine
neue Sitzung auf.

Stabile Fehlercodes der ersten ABI-Version sind:

- `client_invalid_argument`;
- `client_internal_error`;
- `voice_transport_invalid_argument`;
- `voice_transport_invalid_state`;
- `voice_transport_connection_failed`;
- `voice_transport_audio_device_unavailable`;
- `voice_transport_audio_device_switch_failed`;
- `voice_transport_publication_failed`;
- `voice_transport_internal_error`.

Der Diagnosewert `message` ist für Logs und Entwicklung bestimmt und darf nicht
als stabiler Programmiervertrag oder unlokalisierter UI-Text verwendet werden.

## Threading und Lebensdauer

Lebenszyklusoperationen auf demselben Handle werden vom Host serialisiert.
Transportcallbacks dürfen auf Transport- oder Audiothreads eintreffen; der
Client-Core synchronisiert seinen Zustand und vergibt die Ereignissequenz
atomar.

Der Ereigniscallback:

- darf Daten kopieren oder an eine UI-/Game-Queue übergeben;
- darf nicht blockierend auf den aufrufenden Transportthread warten;
- darf das auslösende Handle nicht zerstören;
- darf keine Exception über die C-Grenze propagieren;
- darf keine empfangenen Zeiger nach seiner Rückkehr verwenden.

Vor `hvc_client_core_destroy()` stoppt der Host eigene Aufrufe. Die DLL meldet
den Transportobserver synchron ab; erst danach darf der Host seine
Transportressourcen freigeben.

## Sichere Integrationsreihenfolge

Eine Produktionsintegration verwendet die serverautoritative Reihenfolge:

1. gerätegebundene Session über die Control Plane erstellen;
2. eigene Membership laden und mit `update_membership()` veröffentlichen;
3. kurzlebige Grants derselben Membership-Version abrufen;
4. die ausgegebenen Räume mit `connect()` verbinden;
5. beim PTT-Druck die Transmission serverseitig autorisieren;
6. erst nach positiver, korrelierter Antwort `press_push_to_talk()` aufrufen;
7. beim Loslassen zuerst `release_push_to_talk()` und anschließend die
   Servertransmission beenden;
8. bei Reconnect, Rechteentzug oder Membership-Wechsel keine Transmission
   automatisch neu starten.

Grant-Tokens verbleiben beim Host-Transport. Sie dürfen weder protokolliert noch
in Ereignisse oder UI-Zustände übernommen werden. Membership- und
Sprecherereignisse enthalten nur für die Hostanwendung bestimmte öffentliche
Identitäten, niemals serverintern berechnete Empfängerlisten.

## C++-Integration

Der C++20-Wrapper verwendet `Transport`, `TransportObserver`, `ClientCore` und
besitzende Werttypen:

```cpp
#include <hvc/client_core.hpp>

hvc::client_core::ClientCore core{
    transport,
    [](hvc::client_core::Event event) {
        event_queue.push(std::move(event));
    }};

const hvc::client_core::RoomGrant team{
    hvc::client_core::Scope::team, voice_url, short_lived_token};
const auto connected = core.connect(std::span{&team, 1});
```

Eine vollständig kompilierte, absichtlich transportneutrale Beispielanwendung
liegt unter
[`examples/client-core/basic_integration.cpp`](../examples/client-core/basic_integration.cpp).
Ihr `GameVoiceTransport` ist ein Platzhalter für einen SDK-Adapter und führt
keine echte Serverautorisierung aus.

Als CMake-Konsument:

```cmake
find_package(Hvc CONFIG REQUIRED)
target_link_libraries(game PRIVATE hvc::client_core)
```

## C-Integration

C-Aufrufer initialisieren jede Struktur mit null, setzen `struct_size` und
`abi_version`, füllen die Transporttabelle und erzeugen danach das opake Handle:

```c
hvc_client_core_config_v1 config = {0};
hvc_client_core* core = NULL;

config.struct_size = sizeof(config);
config.abi_version = HVC_CLIENT_CORE_ABI_VERSION;
config.transport = game_transport_table;
config.event_callback = receive_hvc_event;
config.event_user_data = game_context;

if (hvc_client_core_create(&config, &core) != HVC_CLIENT_CORE_RESULT_OK) {
    /* Keep voice disabled and report an integration error. */
}
```

Der Build prüft `hvc/client_core.h` und ein vollständiges
Transporttabellen-Beispiel zusätzlich explizit im C11-Modus.

## Absicherung

`client.client_core_abi` prüft:

- ABI-Versionsablehnung und opake Handle-Erzeugung;
- Transport-Observer-Anmeldung und synchrone Abmeldung;
- Grant-Übergabe, Verbindung und exklusiven PTT-Lebenszyklus;
- Reconnect ohne automatische Wiederaufnahme;
- strikt steigende Membership-Versionen und besitzende C++-Events;
- Speaker-Admission, Start und Ende;
- synchrone und asynchrone Fehlerereignisse;
- strikt steigende Ereignissequenzen.

Der reguläre Build kompiliert außerdem die Beispielintegration und führt den
C-Header-Check aus. Export- und Installationsprüfung stellen sicher, dass die
DLL beziehungsweise Shared Library, Header und das CMake-Ziel
`hvc::client_core` gemeinsam ausgeliefert werden.
