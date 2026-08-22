# Live Calls

## Behavior

Live Calls presents channels, bridges, endpoints, direction, duration, and permitted call actions as current state. Updates are streamed in the installed application; this website never opens a PBX connection.

## Configuration

Filters cover endpoint, direction, bridge, duration, and state. Search defaults to literal text and offers an adjacent bounded regular-expression builder. Columns, sort, grouping, and redaction preferences persist per user.

## Failure modes and security

Stream loss, permission refusal, stale snapshots, and an ended call are separate states. Call control requires an explicit action and role check. Caller identity and dialed numbers follow configured privacy policy and stay out of diagnostics.

## Verification

Exercise new, bridged, held, transferred, and ended transitions; reconnect and out-of-order events; keyboard action menus; screen-reader row updates; partial permissions; and redacted exports.

## Suggested articles

[Call Detail Records](cdr.md), [Dashboard](dashboard.md), and [Security](../team-calling/security.md).
