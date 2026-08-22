# Voicemail

## Behavior

Voicemail manages mailboxes, greetings, message delivery, transcription availability, retention, notification, and permitted playback or export. Playback never starts automatically.

## Configuration

Guided controls select an existing user and extension, greeting source, message limit, retention, notification route, timezone, and language. Uploads are bounded and locally previewed before transfer.

## Failure modes and security

Full mailbox, missing recording, unsupported audio, delivery refusal, unavailable transcription, and permission failure are separate states. Mailbox credentials and message audio stay out of logs, captures, and ordinary exports.

## Verification

Exercise new, saved, deleted, restored, full, and unavailable states; greeting validation; retention boundaries; notification failures; keyboard playback; redaction; and authoritative mailbox readback.

## Suggested articles

[Users](users.md), [Recordings](../team-calling/recordings.md), and [Announcements](../team-calling/announcements.md).
