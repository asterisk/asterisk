# Automation and Converter

## Behavior

Automation schedules supported settings and actions. The local converter presents categorized adapters for Documents/PDF, Images, Audio, Video, Archives, Structured Data/Spreadsheets, Code/Text, and Binary Encodings. The website shows documentation-only unavailable states; conversion belongs to the installed application.

## Configuration

Schedules use stable identifiers, timezone, dates, times, weekdays, precedence, and local or validated external sources. Converter adapters declare signatures, target, bundled proof, metadata behavior, limits, sandbox, and output validation. Queues use bounded concurrency and resumable records.

## Failure modes and security

Invalid schedule, unavailable source, stale response, unsupported type, missing packaged adapter, malformed input, resource limit, cancellation, and output validation failure remain distinct. No arbitrary shell, network converter, PATH discovery, or partial destination is accepted.

## Verification

Test time boundaries, precedence, offline fallback, every category search, adapter availability proof, byte detection, queue pause/resume/cancel, crash recovery, atomic writes, output reopening, no-network behavior, and negative cases for unbundled adapters.

## Suggested articles

[Settings](settings.md), [Backups](backups.md), and [Local AI](local-ai.md).
