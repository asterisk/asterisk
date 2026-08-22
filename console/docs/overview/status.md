# Status

## Behavior

Status combines application version, PBX connection, control-plane state, documentation revision, release availability, and verification evidence. Pending and unrun work never appears as successful.

## Configuration

Users can filter components, choose refresh frequency, and export the currently filtered evidence. Site-only status is stored locally; live application status comes from authenticated component checks.

## Failure modes and security

An unreachable service, invalid response, missing evidence link, or stale heartbeat is named independently. Status links are allowlisted and credentials never appear in the surface or export.

## Verification

Exercise all status states and transitions, timestamp drift, filter persistence, evidence links, keyboard expansion, and offline fallback. Confirm displayed release facts against the immutable release manifest.

## Suggested articles

[System Health](system-health.md), [Updates](../manage/updates.md), and [Logs](logs.md).
