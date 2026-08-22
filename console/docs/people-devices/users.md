# Users

## Behavior

Users connect a person or service identity to extensions, roles, devices, voicemail, presence, and permitted call actions. Lists support filtering, multi-selection, previewed bulk changes, and complete export.

## Configuration

Guided creation offers existing roles, available extensions, locale, timezone, caller identity, and device assignments from real data. Display labels remain separate from stable identifiers.

## Failure modes and security

Duplicate extension, unavailable role, invalid caller identity, partial bulk update, and control-plane refusal are named per user. Least privilege is the default; credentials remain in the operating-system vault and never enter exports or logs.

## Verification

Test create, edit, deactivate, restore, search, bulk actions, role enforcement, duplicate detection, redaction, keyboard operation, and restart persistence against an independent API readback.

## Suggested articles

[Extensions](../connectivity/extensions.md), [Devices](devices.md), and [Voicemail](voicemail.md).
