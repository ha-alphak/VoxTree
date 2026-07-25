# Hierarchical Voice Communication

Hierarchical Voice Communication is a standalone, real-time voice application
for large multiplayer groups. It routes speech through configurable
organizational scopes such as **team**, **specialization**, and **group** while
keeping membership and permissions authoritative on the server.

The project is intended as a voice-focused alternative to general-purpose
communication platforms. Its transport-independent core is also designed to
support future integration into games and other applications.

> [!IMPORTANT]
> This project is under active development and is not ready for production use.
> The domain model and part of the server control plane are implemented; the
> The UI-independent Windows client core, control-plane client, WinHTTP
> transport, and native LiveKit adapter are implemented; the graphical client
> is still planned.

## Core concepts

Each participant belongs to a configurable hierarchy:

```text
Group
├── Specialization
│   ├── Team
│   └── Team
└── Specialization
    ├── Team
    └── Team
```

A transmission targets exactly one scope:

- **Team** — members of the sender's current team
- **Specialization** — all teams in the sender's specialization
- **Group** — every eligible member of the sender's group

Recipients are resolved from authoritative server-side membership data. A
client cannot choose arbitrary recipients or subscribe to a scope without the
required permission.

## Project goals

- Support groups of at least 200 concurrent participants.
- Keep groups, specializations, and teams strictly isolated.
- Configure transmit and receive permissions independently by role.
- Support dedicated push-to-talk actions for every communication scope.
- Handle membership and permission changes atomically during transmissions.
- Recover connections without automatically resuming an interrupted
  transmission.
- Keep the domain, application, persistence, network, and voice transport layers
  independent.

## Architecture

The project uses C++20 and CMake.

- **Domain core** — hierarchy, memberships, roles, routing, permissions, and
  connection/transmission state machines
- **Control plane** — framework-independent application logic for sessions,
  authorization, rate limits, moderation, timeouts, and audit events
- **Persistence** — SQLite-backed session storage with transactional schema
  migrations
- **Network layer** — versioned HTTP boundary for the Linux control plane
- **Voice transport** — self-hosted LiveKit integration behind the internal
  `IVoiceTransport` abstraction; Mumble remains the fallback option
- **Windows client** — planned WinUI 3 application using C++/WinRT

The initial secure transport model uses separate rooms for group,
specialization, and team scopes. A shared room with selective subscriptions is
considered a later optimization and requires a dedicated security validation.

## Current status

Implemented foundations include:

- validated, data-driven group hierarchies;
- immutable and versioned membership snapshots;
- deterministic recipient resolution and isolation tests;
- independent transmit and receive permissions;
- transport-independent connection and transmission state machines;
- session-bound server authorization;
- atomic transmission lifecycle handling;
- rate limiting, timeouts, moderation termination, and typed audit events;
- SQLite session persistence and schema migrations;
- deterministic routing tests with a 200-participant group; and
- Windows and Debian CI configurations;
- UI-independent client core with exclusive multi-scope PTT coordination;
- opt-in native LiveKit transport with room, audio, device, and reconnect
  handling;
- typed control-plane client for sessions, membership, grants, and
  transmissions;
- Windows WinHTTP transport plus authorization-before-audio PTT coordination;
- separate Team, Specialization, and Group PTT bindings; and
- focus-independent Windows keyboard, mouse, gamepad, joystick, and HOTAS
  button capture through Win32 Raw Input and generic HID reports.

The graphical client is not yet complete. See
[the project status](doks/status.md) for the detailed implementation progress.

## Requirements

### All platforms

- CMake 3.25 or newer
- A C++20 compiler

### Windows

- Windows 10 or 11, x64
- Visual Studio 2022
- MSVC v143, Windows SDK, and CMake tools

### Debian

- Debian 13, x64
- GCC and Ninja
- SQLite development files

```bash
sudo apt-get install build-essential cmake libsqlite3-dev ninja-build
```

## Build and test

### Windows

Run a Debug build and all tests:

```powershell
.\scripts\build-windows.cmd -Preset windows-msvc-debug
```

For a Release build:

```powershell
.\scripts\build-windows.cmd -Preset windows-msvc-release
```

### Linux

Run a Debug build with AddressSanitizer and UndefinedBehaviorSanitizer:

```bash
./scripts/build.sh linux-gcc-debug
```

For a Release build:

```bash
./scripts/build.sh linux-gcc-release
```

For static analysis with Clang and `clang-tidy`:

```bash
sudo apt-get install clang clang-tidy
./scripts/build.sh linux-clang-analysis
```

Build output is written to `out/`.

## Documentation

The versioned project documentation is located in [`doks/`](doks/README.md).
Important starting points are:

- [Feature specification](doks/spec.md)
- [Architecture decisions](doks/architecture-decisions.md)
- [Implementation plan](doks/implementation-plan.md)
- [State machines](doks/state-machines.md)
- [Control-plane design](doks/control-plane.md)
- [Development environment](doks/development.md)
- [Current project status](doks/status.md)

## Security and privacy

Memberships, roles, and routing permissions are server-authoritative. Voice
access grants are intended to be short-lived, and clients are never trusted to
provide recipient lists. Membership or permission changes terminate affected
active transmissions.

The system is designed not to store voice content. Audit events contain
transmission metadata and recipient counts, but not internal recipient lists.

Please do not report security vulnerabilities in a public issue. A private
reporting process will be documented before the first public release.

## Contributing

The project is currently in an early development phase. Before opening a pull
request, ensure that the relevant build, tests, formatting checks, and static
analysis pass. More detailed contribution guidelines will be added as the
public API and development workflow stabilize.

## License

No open-source license has been selected yet. Until a license is added, all
rights are reserved by the copyright holder.
