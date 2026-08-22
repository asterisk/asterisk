# Routes

## Behavior

Routes match inbound or outbound calls and send them to a destination through ordered, visible rules. A simulator shows which rule would win without placing a call or changing configuration.

## Configuration

Guided controls select trunks, destinations, caller conditions, number patterns, priority, schedules, and fallback. Pattern syntax identifies its real engine and validates before publication.

## Failure modes and security

Overlapping pattern, unreachable destination, missing trunk, circular route, invalid schedule, and publication refusal remain explicit. Outbound permissions and emergency-routing boundaries cannot be bypassed from the interface.

## Verification

Test exact, prefix, range, fallback, priority, conflict, cycle, and no-match fixtures; simulate before and after publication; verify readback and rollback.

## Suggested articles

[Trunks](../connectivity/trunks.md), [Call Flow](call-flow.md), and [Time Conditions](time-conditions.md).
