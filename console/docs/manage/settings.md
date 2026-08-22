# Settings

## Behavior

Settings provides tabbed, searchable configuration for language, independent English and Cantonese funny levels, dialog emoji, appearance, local attention accommodations, scheduling, personal vocabulary upload, notifications, history, and reset. The website stores only per-visitor browser state.

## Configuration

Every settings tab has search with an adjacent bounded JavaScript regular-expression builder. Language choices are English, playful Hong Kong-style Cantonese, and bilingual. Funny levels range from 1 to 5 and default independently to 5. Personal vocabulary accepts a local, bounded version-1 JSON file and applies nothing until complete validation succeeds.

## Failure modes and security

Malformed or duplicate-key JSON, unsupported version, oversized file, corrupt cache, invalid schedule, unavailable browser storage, and unsupported value remain explicit. Vocabulary content, upload metadata, lock credentials, and ticket descriptions are omitted from exports and network requests.

## Verification

Test all modes and bounds, persistence, clear/reset, no partial vocabulary application, no-network behavior, every search, schedule boundaries, local history, notification review, keyboard operation, screen-reader names, narrow layout, and redacted export.

## Suggested articles

[Appearance](appearance.md), [Accessibility](accessibility.md), and [Automation and Converter](automation.md).
