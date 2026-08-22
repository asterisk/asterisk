# Announcements

## Behavior

Announcements are reusable, named audio messages for menus, queues, routes, and paging. Each record exposes language, duration, format, source, revision, and where it is used.

## Configuration

Users choose an existing validated recording, language, display label, playback count, interruption policy, and destination after playback. Usage references prevent accidental removal.

## Failure modes and security

Missing media, unsupported format, decode failure, inaccessible source, dangling usage, and publication refusal remain explicit. Audio follows retention and access policy and stays out of diagnostics.

## Verification

Exercise valid and malformed audio, language variants, usage references, replacement, rollback, playback controls, no-network behavior, keyboard access, and published configuration readback.

## Suggested articles

[Recordings](recordings.md), [Interactive Voice Response](../call-flow/ivr.md), and [Queues](queues.md).
