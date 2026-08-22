# Extensions

## Behavior

Extensions map stable internal numbers to users, devices, voicemail, permissions, and call-flow destinations. Availability and collision checks happen before a change is offered for publication.

## Configuration

The number picker is populated from the active numbering plan, offers a validated suggestion, and retains advanced manual entry. Assignments, caller identity, dialing permissions, and fallback are explicit.

## Failure modes and security

Duplicate number, reserved range, missing target, unsupported device, partial assignment, and publication refusal are separated. Permissions are enforced by the privileged boundary rather than renderer controls.

## Verification

Test available and conflicting numbers, reserved patterns, assignment changes, rollback, bulk preview, keyboard pickers, narrow layout, and authoritative dialplan readback.

## Suggested articles

[Users](../people-devices/users.md), [Devices](../people-devices/devices.md), and [Routes](../call-flow/routes.md).
