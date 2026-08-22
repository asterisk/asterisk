# System Health

## Behavior

System Health reports process, endpoint, storage, certificate, clock, and integration health with observed time and evidence. Health summaries link to the precise component rather than collapsing unrelated failures into one colour.

## Configuration

Thresholds, refresh intervals, quiet hours, and visible components are locally configurable within validated bounds. Scheduled settings can change presentation but never rewrite measured values.

## Failure modes and security

Unknown is distinct from healthy. A timed-out probe, missing permission, offline PBX, or stale response keeps its own diagnosis and retry path. Diagnostics exclude credentials, private paths, and response bodies containing call data.

## Verification

Test healthy, warning, failed, stale, unknown, permission-refused, and offline states. Compare displayed facts with direct component probes; verify notification history, reduced motion, colour-independent labels, and keyboard access.

## Suggested articles

[Status](status.md), [Logs](logs.md), and [Backups](../manage/backups.md).
