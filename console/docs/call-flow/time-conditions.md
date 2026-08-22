# Time Conditions

## Behavior

Time Conditions switch call destinations by local date, time, weekday, holiday, and explicit exception. A preview explains the currently winning rule and next transition.

## Configuration

Rules have stable identifiers, labels, enabled state, timezone, optional dates, start and end time, every-day or selected weekdays, priority, active destination, and fallback. Later equal-priority rules win deterministically.

## Failure modes and security

Invalid partial date, empty weekday set, equal start and end, missing timezone, unavailable external source, and conflicting priority are explained before publication. External credentials remain in the operating-system vault.

## Verification

Test cross-midnight windows, daylight-saving boundaries, date limits, exceptions, empty schedules, precedence, offline fallback, restart persistence, and simulator/readback agreement.

## Suggested articles

[Routes](routes.md), [Call Flow](call-flow.md), and [Settings](../manage/settings.md).
