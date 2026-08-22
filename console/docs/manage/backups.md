# Backups

## Behavior

Backups creates versioned, integrity-recorded snapshots of explicitly selected configuration and application data, then verifies that the archive can be read. Restore is a separate previewed action.

## Configuration

Users choose scope, destination through a path picker, schedule, retention, compression, optional user-supplied encryption, and pre-restore snapshot. Every selection states included and excluded data.

## Failure modes and security

Insufficient storage, unwritable destination, partial source, invalid archive, wrong credential, version incompatibility, and failed restore remain distinct. Credentials never enter settings, logs, arguments, or backup metadata.

## Verification

Test full and scoped backup, storage preflight, cancellation, corruption, retention, encryption refusal, restore preview, rollback, restart recovery, and independent post-restore state checks.

## Suggested articles

[Settings](settings.md), [Security](../team-calling/security.md), and [System Health](../overview/system-health.md).
