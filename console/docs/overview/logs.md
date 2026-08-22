# Logs

## Behavior

Logs present timestamped component events with severity, source, correlation identifier, and structured details. Virtualized rows preserve performance while search, date, severity, and component filters compose.

## Configuration

Users choose visible sources, retention window, wrapping, density, and redaction. Export states encoding, time range, active filters, and omitted sensitive fields.

## Failure modes and security

Unavailable source, rotation gap, malformed entry, parse fallback, and permission refusal are distinct. Secrets, credential headers, private vocabulary payloads, and raw call content are excluded before storage and display.

## Verification

Test large streams, rotation, malformed input, bounded regular expressions, filter composition, cancellation, redaction fixtures, keyboard traversal, and exact exported range.

## Suggested articles

[System Health](system-health.md), [Status](status.md), and [Security](../team-calling/security.md).
