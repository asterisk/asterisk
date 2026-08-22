# Trunks

## Behavior

Trunks represent provider or peer connections with registration, endpoint, transport, codec, capacity, failover, and observed health. A trunk is not marked available from configuration alone.

## Configuration

Guided forms offer detected transports, supported codecs, validated host and port fields, authentication methods, keepalive, limits, and failover order. Test calls are explicit actions.

## Failure modes and security

DNS failure, transport refusal, authentication failure, codec mismatch, certificate issue, capacity exhaustion, and stale registration remain distinct. Secrets stay in the operating-system vault and are redacted everywhere else.

## Verification

Exercise registration, failure, recovery, failover order, codec negotiation, bounded retries, certificate validation, role enforcement, and independent provider or Asterisk readback.

## Suggested articles

[Routes](../call-flow/routes.md), [System Health](../overview/system-health.md), and [Security](../team-calling/security.md).
