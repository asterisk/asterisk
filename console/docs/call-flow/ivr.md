# Interactive Voice Response

## Behavior

Interactive Voice Response builds accessible audio menus with prompts, digit choices, timeout, invalid-choice handling, repeat limits, and fallback destinations. A local simulator previews structure and timing without placing a call.

## Configuration

Users choose existing recordings, language, bounded digit sequences, timeouts, repeats, direct dialing policy, and destinations from real lists. Every disabled choice explains its unmet condition.

## Failure modes and security

Missing prompt, duplicate choice, invalid destination, timeout loop, unsupported audio, and publication refusal are named. Sensitive destinations remain permission-filtered and prompt files are validated before use.

## Verification

Exercise every digit, invalid input, timeout, repeat ceiling, direct dialing, language mode, missing audio, simulator parity, rollback, keyboard operation, and screen-reader labels.

## Suggested articles

[Recordings](../team-calling/recordings.md), [Announcements](../team-calling/announcements.md), and [Call Flow](call-flow.md).
