# Ring Groups

## Behavior

Ring Groups call multiple permitted destinations in parallel or sequence and follow an explicit no-answer fallback. A preview lists order, timing, and protected members.

## Configuration

Users choose real extensions and external destinations, ring strategy, timeout, confirmation behavior, caller identity, and fallback. Reordering supports pointer and keyboard actions.

## Failure modes and security

Empty membership, duplicate target, circular fallback, unreachable member, unsupported confirmation, and publication refusal are distinct. External destinations require the same dialing permission as ordinary outbound calls.

## Verification

Test parallel and sequence order, timeout, busy and unavailable members, confirmation, fallback, cycle detection, bulk membership, rollback, and authoritative dialplan readback.

## Suggested articles

[Queues](queues.md), [Extensions](../connectivity/extensions.md), and [Call Flow](../call-flow/call-flow.md).
