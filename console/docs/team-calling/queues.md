# Queues

## Behavior

Queues distribute waiting callers to available members through a named strategy, announce truthful wait state, and expose current membership, callers, service levels, and fallbacks.

## Configuration

Guided controls select members, strategy, capacity, retry timing, music, periodic announcements, timeout, wrap-up, penalties, and fallback from verified options. Changes are previewed before publication.

## Failure modes and security

No available member, full queue, missing audio, unreachable fallback, stale membership, and publication refusal remain distinct. Caller identity and member performance follow role-based access and export redaction.

## Verification

Exercise every strategy, join and leave, capacity, timeout, priority, wrap-up, member pause, fallback, partial failure, simulation, readback, and accessible live updates.

## Suggested articles

[Ring Groups](ring-groups.md), [Announcements](announcements.md), and [Reports](../overview/reports.md).
