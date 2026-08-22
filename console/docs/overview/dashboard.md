# Dashboard

## Behavior

The Dashboard brings service health, active calls, recent configuration changes, alerts, and a user-chosen next action into one evidence-backed view. Cards reveal their source and last update; an unavailable source remains unavailable rather than displaying sample success data.

## Configuration

Users can reorder and pin cards, choose density, enable independent attention accommodations, and filter activity with plain text or the adjacent JavaScript regular-expression builder. Layout and card choices persist locally without changing PBX configuration.

## Failure modes and security

Disconnected control-plane, stale data, authorization refusal, and partial-source failures remain distinct. Sensitive call identities are redacted according to the signed-in role. Dashboard cards never carry credentials or execute configuration changes.

## Verification

Verify keyboard card navigation, narrow and high-scale layouts, timestamps, stale-state transitions, redaction, empty state, and restoration of card order after restart. Confirm every value agrees with an independent source response.

## Suggested articles

[System Health](system-health.md), [Live Calls](live-calls.md), and [Status](status.md).
