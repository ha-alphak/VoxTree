# Hierarchical Voice Communication

## Software Feature Specification

**Document status:** Draft  
**Target audience:** Software engineers, system architects, QA engineers, game designers  
**Feature name:** Hierarchical Voice Communication  
**Feature type:** Real-time multiplayer voice communication  

---

## 1. Purpose

Hierarchical Voice Communication provides structured voice routing for large multiplayer groups.

Players are organized into multiple hierarchy levels:

1. **Group** — the complete player organization.
2. **Specialization** — a subdivision of the group, such as Red, Green, or Blue.
3. **Team** — a small unit within a specialization, typically containing up to five players.

A player can transmit voice to one of these scopes. The system must deliver the transmission to every eligible player within the selected scope.

Example:

- A **Team** transmission is heard only by members of the sender's team.
- A **Specialization** transmission is heard by all members of the sender's specialization.
- A **Group** transmission is heard by all members of the complete group.

---

## 2. Goals

The feature must:

- Support voice communication in groups of at least 200 concurrent players.
- Route voice based on the sender's selected communication scope.
- Prevent players outside the selected scope from receiving the transmission.
- Allow authorized players to communicate with larger hierarchy levels.
- Clearly indicate the currently selected transmission scope.
- Handle simultaneous transmissions without corrupting or incorrectly routing audio.
- Integrate with existing mute, block, moderation, and audio-volume systems.

---

## 3. Non-Goals

The initial implementation does not need to provide:

- Speech-to-text transcription.
- Automatic translation.
- Voice recording or replay.
- Proximity-based voice chat.
- Cross-group communication.
- AI-based noise classification.
- Persistent voice channels after the player leaves the group.

These capabilities may be added separately.

---

## 4. Terminology

| Term | Definition |
|---|---|
| **Group** | The top-level collection of all participating players. |
| **Specialization** | A logical subdivision of the group, such as Red, Green, or Blue. |
| **Team** | A small unit within a specialization, normally containing up to five players. |
| **Scope** | The audience selected for an outgoing voice transmission. |
| **Sender** | The player transmitting voice audio. |
| **Recipient** | A player eligible to receive a transmission. |
| **Membership** | The player's current assignment to a group, specialization, and team. |
| **Transmit Permission** | Authorization to send audio to a specific scope. |
| **Receive Permission** | Authorization to receive audio from a specific scope. |
| **Push-to-Talk** | An input mode in which a player transmits while holding a configured control. |

---

## 5. Hierarchy Model

The default hierarchy is:

```text
Group
├── Red Specialization
│   ├── Red Team 1
│   ├── Red Team 2
│   └── ...
├── Green Specialization
│   ├── Green Team 1
│   ├── Green Team 2
│   └── ...
└── Blue Specialization
    ├── Blue Team 1
    ├── Blue Team 2
    └── ...
```

Each player must belong to:

- Exactly one group.
- Exactly one specialization within that group.
- Exactly one team within that specialization.

The data model should permit configurable names and should not hard-code Red, Green, or Blue.

---

## 6. Communication Scopes

### 6.1 Team Scope

A Team transmission is delivered to:

- The sender.
- All connected players assigned to the sender's current team.

It is not delivered to:

- Other teams in the same specialization.
- Other specializations.
- Other groups.

### 6.2 Specialization Scope

A Specialization transmission is delivered to:

- The sender.
- All connected players assigned to any team within the sender's current specialization.

It is not delivered to:

- Other specializations.
- Other groups.

### 6.3 Group Scope

A Group transmission is delivered to:

- The sender.
- All connected players assigned to the sender's current group.

It is not delivered to:

- Players in other groups.
- Spectators without explicit receive permission.

---

## 7. Functional Requirements

### FR-001: Scope Selection

The client must allow the player to select one of the available transmission scopes:

- Team
- Specialization
- Group

The currently selected scope must be visible in the user interface.

### FR-002: Push-to-Talk Bindings

The system should support either of the following input models:

1. One Push-to-Talk control combined with a selected scope.
2. A separate Push-to-Talk control for each scope.

The implementation should allow the game to enable either model through configuration.

Recommended default bindings:

| Action | Example binding |
|---|---|
| Team Push-to-Talk | `V` |
| Specialization Push-to-Talk | `B` |
| Group Push-to-Talk | `N` |

Bindings must be configurable by the player.

### FR-003: Recipient Resolution

When a transmission starts, the authoritative voice-routing service must determine the recipient set from the sender's current membership and selected scope.

Recipient resolution must not rely exclusively on client-provided recipient identifiers.

### FR-004: Membership Validation

Before transmitting audio, the service must verify that:

- The sender is connected.
- The sender belongs to an active group.
- The requested scope exists.
- The sender has permission to use the requested scope.
- The sender is not globally voice-muted or voice-banned.

### FR-005: Dynamic Membership Changes

When a player changes team, specialization, or group:

- The player's voice memberships must be updated immediately.
- The player must stop receiving transmissions for the previous membership.
- The player must begin receiving transmissions for the new membership.
- An active transmission should either be terminated or atomically rerouted according to a configured policy.

Recommended policy: terminate the active transmission and require the sender to start a new one.

### FR-006: Permission-Based Transmission

Transmission permissions must be configurable per role and scope.

Example default policy:

| Role | Team | Specialization | Group |
|---|---:|---:|---:|
| Player | Allowed | Not allowed | Not allowed |
| Team Leader | Allowed | Allowed | Not allowed |
| Specialization Leader | Allowed | Allowed | Allowed |
| Group Leader | Allowed | Allowed | Allowed |
| Moderator | Configurable | Configurable | Configurable |

The exact role names and permissions must be data-driven.

### FR-007: Receive Permissions

The system must support receive restrictions independently from transmit permissions.

Examples:

- A spectator may receive Group transmissions but may not transmit.
- A muted player may receive voice but may not transmit.
- A moderator may listen to a scope without being a normal member, if explicitly authorized.

### FR-008: Simultaneous Speakers

The client must support multiple simultaneous incoming speakers.

The implementation must define configurable limits for:

- Maximum simultaneous decoded streams.
- Maximum simultaneous speakers displayed in the user interface.
- Maximum simultaneous speakers per scope.

The default should prioritize higher-scope transmissions:

1. Group
2. Specialization
3. Team

Priority must affect audio ducking or stream admission only. It must not incorrectly change recipient membership.

### FR-009: Audio Ducking

When a higher-priority transmission is active, lower-priority voice audio may be reduced in volume.

Recommended defaults:

- Group transmission ducks Specialization and Team audio.
- Specialization transmission ducks Team audio.
- Team transmission does not duck higher scopes.

Ducking values must be configurable.

### FR-010: Sender Feedback

While transmitting, the client must display:

- Transmission-active status.
- Selected scope.
- A microphone activity indicator.
- An error state if the transmission is rejected.

Optional feedback:

- Number of intended recipients.
- Role or permission used for the transmission.

### FR-011: Recipient Feedback

While receiving a transmission, the client should display:

- Speaker name.
- Speaker icon or avatar.
- Transmission scope.
- Speaking state.

Scope indicators must be visually distinguishable.

### FR-012: Player Mute and Block

Existing player-level mute and block settings must be respected.

A locally muted sender's audio must not be played on that recipient's client, even when the sender uses Group scope.

Whether emergency moderator announcements can bypass local mute must be a separate explicit feature and must not be assumed by this specification.

### FR-013: Moderation

Authorized moderators must be able to:

- Revoke a player's transmit permission.
- Temporarily mute a player.
- Permanently voice-ban a player.
- Inspect scope and sender metadata for active transmissions.

Moderation actions must be logged.

### FR-014: Connection Recovery

After a temporary network interruption, the client must:

- Reconnect to the voice service.
- Refresh its authoritative membership.
- Reapply receive subscriptions.
- Restore the selected local transmission scope.
- Not automatically resume a previously active transmission.

### FR-015: Configuration

The following values must be configurable:

- Hierarchy depth.
- Scope names.
- Maximum group size.
- Maximum team size.
- Role-to-scope permissions.
- Audio priority.
- Ducking levels.
- Maximum concurrent speakers.
- Push-to-Talk behavior.
- Open-microphone availability.
- Transmission timeout.

---

## 8. Routing Rules

The routing service must calculate recipients using authoritative server-side membership data.

### 8.1 Team Recipient Rule

```text
recipient.groupId == sender.groupId
AND recipient.specializationId == sender.specializationId
AND recipient.teamId == sender.teamId
```

### 8.2 Specialization Recipient Rule

```text
recipient.groupId == sender.groupId
AND recipient.specializationId == sender.specializationId
```

### 8.3 Group Recipient Rule

```text
recipient.groupId == sender.groupId
```

### 8.4 Common Eligibility Rule

A matching player is an eligible recipient only when:

```text
recipient.isConnected == true
AND recipient.canReceiveVoice == true
AND sender is not locally blocked or muted by recipient
```

Local mute and block checks may be enforced client-side, but the server should avoid sending streams where policy or privacy requirements demand server-side filtering.

---

## 9. Suggested Data Model

### 9.1 Player Voice Membership

```typescript
interface VoiceMembership {
  playerId: string;
  groupId: string;
  specializationId: string;
  teamId: string;
  roleIds: string[];
  canReceiveVoice: boolean;
  voiceBanStatus: "none" | "temporary" | "permanent";
  membershipVersion: number;
}
```

### 9.2 Voice Scope

```typescript
type VoiceScopeType = "team" | "specialization" | "group";

interface VoiceScope {
  id: string;
  type: VoiceScopeType;
  displayName: string;
  priority: number;
  maxConcurrentSpeakers?: number;
}
```

### 9.3 Transmission Request

```typescript
interface StartVoiceTransmissionRequest {
  senderPlayerId: string;
  requestedScope: VoiceScopeType;
  clientTransmissionId: string;
  membershipVersion: number;
  clientTimestamp: number;
}
```

The client must not provide a trusted recipient list.

### 9.4 Transmission Session

```typescript
interface VoiceTransmissionSession {
  transmissionId: string;
  senderPlayerId: string;
  scope: VoiceScopeType;
  groupId: string;
  specializationId?: string;
  teamId?: string;
  startedAt: number;
  recipientCount: number;
  status: "active" | "ended" | "rejected";
}
```

---

## 10. Suggested Service Interfaces

The exact API depends on the selected voice technology. The following messages describe the required behavior.

### 10.1 Start Transmission

```http
POST /voice/transmissions
```

```json
{
  "requestedScope": "specialization",
  "clientTransmissionId": "6fc13f6c-5f07-4be2-b77f-fac672dc52df",
  "membershipVersion": 42
}
```

Successful response:

```json
{
  "transmissionId": "tx_12345",
  "scope": "specialization",
  "recipientCount": 65,
  "status": "active"
}
```

Rejected response:

```json
{
  "errorCode": "VOICE_SCOPE_NOT_AUTHORIZED",
  "message": "The player is not authorized to transmit to this scope."
}
```

### 10.2 End Transmission

```http
DELETE /voice/transmissions/{transmissionId}
```

The service must also end a transmission when:

- Push-to-Talk is released.
- The sender disconnects.
- The sender changes membership.
- The transmission timeout is reached.
- A moderator revokes permission.
- The voice transport fails.

### 10.3 Membership Update Event

```json
{
  "eventType": "voice.membership.updated",
  "playerId": "player_1001",
  "groupId": "group_alpha",
  "specializationId": "red",
  "teamId": "red_team_03",
  "membershipVersion": 43
}
```

---

## 11. State Model

A local sender can be in one of the following states:

```text
Idle
  -> RequestingTransmission
  -> Transmitting
  -> EndingTransmission
  -> Idle
```

Error transitions:

```text
RequestingTransmission -> Rejected -> Idle
Transmitting -> Interrupted -> Idle
Transmitting -> PermissionRevoked -> Idle
Transmitting -> MembershipChanged -> Idle
```

The client must not display the player as actively transmitting until the voice service accepts the request or the underlying voice transport confirms successful publication.

---

## 12. Error Codes

| Error code | Meaning |
|---|---|
| `VOICE_NOT_CONNECTED` | The client is not connected to the voice service. |
| `VOICE_NO_ACTIVE_MEMBERSHIP` | The player has no valid group membership. |
| `VOICE_SCOPE_NOT_FOUND` | The requested scope is not configured. |
| `VOICE_SCOPE_NOT_AUTHORIZED` | The player lacks transmit permission for the scope. |
| `VOICE_TRANSMIT_MUTED` | The player is currently muted or voice-banned. |
| `VOICE_MEMBERSHIP_STALE` | The client's membership version is outdated. |
| `VOICE_TRANSMISSION_LIMIT` | The maximum number of speakers has been reached. |
| `VOICE_RATE_LIMITED` | The player started or stopped transmissions too frequently. |
| `VOICE_TRANSPORT_ERROR` | The underlying voice transport failed. |
| `VOICE_INTERNAL_ERROR` | An unexpected service error occurred. |

Errors shown to the player should use localized, user-friendly messages.

---

## 13. Security Requirements

The system must:

- Treat the server as authoritative for membership and permissions.
- Authenticate all voice-service connections.
- Prevent clients from subscribing to unauthorized channels.
- Prevent clients from publishing directly to arbitrary scope identifiers.
- Use short-lived voice access tokens.
- Validate every transmission request against current membership.
- Invalidate permissions after role or membership changes.
- Rate-limit transmission start and stop requests.
- Avoid exposing hidden player identifiers in client-visible metadata.
- Log permission failures and suspicious subscription attempts.

A malicious client must not be able to hear or address another team, specialization, or group by modifying local request data.

---

## 14. Privacy Requirements

The implementation should minimize voice metadata exposure.

Recipients may receive only the metadata required to render the transmission:

- Public player identifier or display name.
- Scope type.
- Speaking state.
- Optional role indicator.

Voice audio must not be stored unless a separate recording feature, legal basis, retention policy, and user notification mechanism are implemented.

---

## 15. Performance Requirements

The system must support at least:

- 200 connected players per group.
- Three or more specializations per group.
- Teams of up to five players by default.
- Multiple simultaneous transmissions across independent teams.
- One Group transmission reaching all eligible group members.

Target performance:

| Metric | Target |
|---|---:|
| Transmission setup latency | Less than 300 ms under normal conditions |
| Membership update propagation | Less than 1 second |
| Scope switch response | Less than 250 ms |
| Voice routing availability | At least 99.9% during active game sessions |
| Incorrect recipient rate | 0 tolerated |

Audio latency should be measured separately from transmission authorization latency.

---

## 16. Scalability Considerations

A separate physical voice room for every possible scope may create excessive subscription and operational overhead. The implementation should evaluate one of these models:

### Model A: Separate Voice Rooms

- One room per team.
- One room per specialization.
- One room per group.
- Clients subscribe to all rooms they are authorized to receive.

Advantages:

- Conceptually simple.
- Compatible with many existing voice providers.

Disadvantages:

- Multiple room subscriptions per client.
- More complex reconnection behavior.
- Potentially higher provider costs.

### Model B: Shared Room with Server-Controlled Routing

- One voice room per group.
- Each published stream contains validated scope metadata.
- A server-side forwarding unit routes streams to authorized recipients.

Advantages:

- Fewer room connections.
- Centralized routing and permission control.
- Easier hierarchy expansion.

Disadvantages:

- Requires a capable selective forwarding architecture.
- More complex routing logic.

### Recommended Approach

Use a shared group-level selective forwarding unit with server-authoritative subscription and publication permissions. Represent Team and Specialization communication as server-controlled routing scopes rather than trusting client-managed channels.

---

## 17. User Interface Requirements

The voice interface should include:

- Current transmission scope.
- Scope-selection control.
- Push-to-Talk binding hints.
- Active speaker list.
- A scope icon or label for each speaker.
- Permission-denied feedback.
- Connection-state feedback.

Suggested labels:

| Scope | UI label |
|---|---|
| Team | `TEAM` |
| Specialization | `RED`, `GREEN`, `BLUE`, or configured name |
| Group | `GROUP` |

The interface must not imply that a transmission was delivered until the transmission request was accepted.

---

## 18. Accessibility Requirements

The user interface should:

- Not rely exclusively on color to identify a scope.
- Provide text labels or distinct icons.
- Support remappable controls.
- Support independent incoming voice volume.
- Provide a visual speaking indicator.
- Provide optional sound cues for transmission start, stop, and rejection.

---

## 19. Telemetry and Logging

The system should collect the following non-audio telemetry:

- Transmission start and end timestamps.
- Selected scope.
- Sender role.
- Recipient count.
- Transmission rejection reason.
- Setup latency.
- Transport failure rate.
- Membership version mismatch rate.
- Concurrent speaker count.

The system must not log voice content.

Logs should include a correlation identifier for tracing a transmission across the game server, permission service, and voice infrastructure.

---

## 20. Edge Cases

The implementation must define behavior for the following cases:

### 20.1 Sender Has No Team

If Team scope is requested without a valid team assignment, reject the request with `VOICE_NO_ACTIVE_MEMBERSHIP`.

### 20.2 Empty Recipient Set

The sender may still receive local transmission feedback, but the service should return a recipient count of zero. The game may optionally warn the player.

### 20.3 Sender Changes Team While Speaking

Terminate the transmission immediately. Do not continue routing it to either the old or new team.

### 20.4 Permission Revoked While Speaking

Terminate the transmission immediately and notify the sender.

### 20.5 Group Dissolved

Terminate all active transmissions for that group and disconnect or reassign voice memberships.

### 20.6 Two Group Leaders Speak Simultaneously

Both streams may be delivered if the configured concurrency limit permits it. Otherwise, apply the configured admission or priority policy.

### 20.7 Client Uses Stale Membership

Reject the request with `VOICE_MEMBERSHIP_STALE`, refresh membership, and allow the player to retry.

### 20.8 Local Mute During Active Transmission

Stop playback immediately on the muting recipient's client without affecting other recipients.

---

## 21. Acceptance Criteria

### AC-001: Team Routing

**Given** a player belongs to Red Team 1  
**When** the player transmits using Team scope  
**Then** only connected and eligible members of Red Team 1 receive the audio.

### AC-002: Specialization Routing

**Given** a player belongs to the Red specialization  
**When** the player transmits using Specialization scope  
**Then** all connected and eligible players in Red receive the audio  
**And** no player in Green or Blue receives it.

### AC-003: Group Routing

**Given** a player belongs to Group Alpha  
**When** the player transmits using Group scope  
**Then** all connected and eligible players in Group Alpha receive the audio.

### AC-004: Unauthorized Scope

**Given** a normal player may transmit only to Team scope  
**When** the player requests Group scope  
**Then** the service rejects the request  
**And** no recipient receives audio  
**And** the client displays an authorization error.

### AC-005: Membership Change

**Given** a player is receiving Red Team 1 audio  
**When** the player is moved to Red Team 2  
**Then** the player stops receiving Red Team 1 transmissions  
**And** begins receiving Red Team 2 transmissions.

### AC-006: Cross-Group Isolation

**Given** two separate groups exist  
**When** a player sends a Group transmission in Group Alpha  
**Then** no player in Group Beta receives audio or transmission metadata.

### AC-007: Local Mute

**Given** Recipient A has locally muted Sender B  
**When** Sender B sends a valid Group transmission  
**Then** Recipient A does not hear Sender B  
**And** other eligible recipients continue to hear Sender B.

### AC-008: Reconnection

**Given** a player's voice connection is interrupted  
**When** the connection is restored  
**Then** the client refreshes membership and subscriptions  
**And** does not automatically resume transmitting.

### AC-009: Server Authority

**Given** a modified client submits arbitrary recipient identifiers  
**When** it requests a transmission  
**Then** the service ignores the supplied recipients  
**And** computes the valid recipient set from authoritative membership data.

### AC-010: Large Group

**Given** a group contains 200 connected players  
**When** an authorized player sends a Group transmission  
**Then** all eligible recipients can receive the transmission within the defined latency target.

---

## 22. Test Scenarios

The QA plan should include:

- Team-to-team isolation tests.
- Specialization isolation tests.
- Cross-group isolation tests.
- Permission matrix tests for every role and scope.
- Membership changes during active transmissions.
- Disconnect and reconnect tests.
- Simultaneous speaker load tests.
- 200-player Group transmission load tests.
- Malicious client subscription tests.
- Local mute and block tests.
- Moderator mute and permission-revocation tests.
- Voice-provider outage tests.
- Packet loss and high-latency tests.
- Scope UI state and error-message tests.

---

## 23. Implementation Notes

- Keep hierarchy configuration separate from the voice transport implementation.
- Use immutable or versioned membership snapshots for transmission authorization.
- Prefer server-issued, short-lived publication and subscription grants.
- Do not use client-side channel names as security boundaries.
- Ensure voice-routing changes are atomic from the player's perspective.
- Design scope identifiers so additional hierarchy levels can be added later.
- Keep audio transport, access control, membership, and user-interface logic as separate modules.

---

## 24. Future Extensions

Potential future extensions include:

- Multiple specializations per player.
- Temporary task-force channels.
- Leader-only channels.
- Cross-specialization command channels.
- Proximity voice combined with hierarchical voice.
- Priority or emergency announcements.
- Speech-to-text accessibility.
- Voice activity detection.
- Console and mobile support.
- Automated moderation signals.

---

## 25. Open Design Decisions

The engineering and game-design teams must decide:

1. Whether every player may use every scope or scopes are role-restricted.
2. Whether scope selection uses separate keys or a scope selector.
3. Whether higher-scope messages duck or suppress lower-scope messages.
4. How many simultaneous incoming speakers are supported.
5. Whether spectators can receive any scopes.
6. Whether active transmissions end when membership changes.
7. Whether the implementation uses separate rooms or selective forwarding.
8. Whether recipient count is visible to the sender.
9. Whether Group communication has a maximum transmission duration.
10. Whether moderation announcements require a separate, non-mutable emergency scope.

---

## 26. Definition of Done

The feature is complete when:

- All functional requirements are implemented or explicitly deferred.
- All acceptance criteria pass in automated or documented manual tests.
- Unauthorized cross-scope and cross-group access is prevented.
- The system passes the agreed 200-player load test.
- Membership updates are reflected within the defined target time.
- Client UI clearly shows transmit and receive scope information.
- Moderation and telemetry integration is operational.
- Technical documentation and configuration examples are available.
