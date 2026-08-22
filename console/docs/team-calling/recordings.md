# Recordings

## Behavior

Recordings manages prompts and permitted call recordings with source, duration, format, consent status, retention, usage, playback, export, and deletion safeguards.

## Configuration

Uploads and captures have bounded size and duration, decoded-byte validation, language, category, retention, and explicit consent metadata. Lists support previewed bulk move, tag, export, and delete.

## Failure modes and security

Unsupported audio, malformed bytes, storage exhaustion, missing consent, unavailable playback, retention conflict, and partial batch result stay distinct. Audio and caller identity never enter logs or ordinary captures.

## Verification

Test valid and malformed formats, limits, atomic storage, consent requirements, retention, usage protection, exports, destructive confirmation, rollback, keyboard playback, and role-based redaction.

## Suggested articles

[Announcements](announcements.md), [Voicemail](../people-devices/voicemail.md), and [Call Detail Records](../overview/cdr.md).
