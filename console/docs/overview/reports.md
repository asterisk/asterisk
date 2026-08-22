# Reports

## Behavior

Reports summarize call volume, outcomes, queue performance, trunk usage, and time ranges with accessible tables alongside charts. Units, source, timezone, and last update accompany every result.

## Configuration

Users choose date ranges, grouping, metrics, comparison periods, and visible series. Filters compose with the regular-expression search and exports preserve the active scope.

## Failure modes and security

No data, partial data, delayed ingestion, unsupported metric, and authorization refusal are distinct. Small groups and sensitive identities can be suppressed by policy before data reaches the renderer.

## Verification

Validate aggregation against known fixtures, timezone and daylight-saving boundaries, empty and partial states, chart/table equivalence, keyboard data inspection, and CSV/JSON export round trips.

## Suggested articles

[Call Detail Records](cdr.md), [Queues](../team-calling/queues.md), and [Trunks](../connectivity/trunks.md).
