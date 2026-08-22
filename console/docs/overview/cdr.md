# Call Detail Records

## Behavior

Call Detail Records list completed call events with direction, endpoints, timestamps, duration, disposition, linked identifiers, and recording references where permitted. Search and filters stay visibly active.

## Configuration

Date range, columns, redaction, page size, retention view, and export format are configurable. Bulk export applies only to the previewed filtered selection and reports skipped records.

## Failure modes and security

Incomplete correlation, missing recording, delayed records, invalid time ranges, and unavailable backends remain explicit. Exports follow role-based redaction and never include credentials or hidden fields.

## Verification

Verify correlation across linked legs, timezone boundaries, zero-duration calls, pagination, selection scope, regular-expression bounds, redacted export, and screen-reader table semantics.

## Suggested articles

[Live Calls](live-calls.md), [Reports](reports.md), and [Recordings](../team-calling/recordings.md).
