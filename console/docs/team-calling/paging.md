# Paging

## Behavior

Paging sends a live announcement to a defined device group with one-way or supported duplex behavior, priority, current state, and an explicit stop action.

## Configuration

Users select verified compatible devices, mode, priority, timeout, preamble, volume policy, and emergency restrictions. Unsupported device capabilities remain visible and disabled with reasons.

## Failure modes and security

No compatible device, partial reachability, permission refusal, device busy, unsupported duplex, and timeout are reported per target. Paging requires a privileged action and maintains an auditable event record.

## Verification

Test one-way and duplex-capable groups, partial delivery, device busy, timeout, cancellation, priority conflicts, permission enforcement, accessibility, and independent endpoint-state confirmation.

## Suggested articles

[Devices](../people-devices/devices.md), [Announcements](announcements.md), and [Security](security.md).
