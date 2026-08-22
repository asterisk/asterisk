# Ding PBX delivery handoff

## Scope

This handoff covers repository delivery automation and public-safe records only: root bootstrap/build scripts, GitHub Actions, line counting, completeness and design-parity inventories, contributor guidance, and release evidence contracts.

## Implemented

- Pinned Node.js `22.23.2` for Windows x64 from the official Node.js release service with SHA-256 `1177b4137ba5adaa56354ae40f1080c7450e8ae09cecb47da459d1c52ac99f97`.
- Added silent modes `/s`, `--silent`, and `SILENT=1`; user-scoped extraction under local application data; digest verification; reproducible `npm ci`; phase timing; and actionable failure output.
- Added runnable-build and installer-build entry points. Packaging clears signing inputs, invokes the product's Squirrel.Windows script, checks required files and the `RELEASES` index, verifies `Setup.exe` is `NotSigned`, and prints file sizes and SHA-256 values.
- Replaced inherited workflows with one Windows-only build/package/Pages/release workflow. It probes repository self-hosted runner availability and otherwise uses pinned `windows-2025`. The observed repository inventory contained zero self-hosted runners; organization inventory was unavailable with HTTP 403, so the workflow does not claim organization-runner evidence.
- Added one unique monotonic `ding-pbx-console-v0.0.<run>-r<attempt>` release per successful push or manual run, exact target verification, non-draft verification, required-asset verification, workflow start/completion/duration, SHA-256 records, and safe always-upload evidence.
- Added a committed line counter that separates project source, tests, markup, generated output, and inherited/vendor source, with surviving-line authorship for project files.

## Independent audit baseline

- Original design archive SHA-256: `9A4284745A745C18A18B0A23D2A2F5851A79F9B6EFCBC5EE30EDCD69CEA2863F`.
- Destinations: `32`.
- Navigation rail counts: `8 / 4 / 2 / 4 / 7 / 7`.
- Declarative bindings: `265` total: `212` click, `10` change, `10` input, `9` context menu, `4` each drag start/over/drop/end, `5` mouse down, and `1` each mouse enter/leave/up.
- Distinct expressions: `168`.
- Controls: `479`.
- Transient-state families: `17`.

## Verification state

- Repository-local syntax, manifest, inventory-schema, negative-regression, and workflow-structure checks are expected before this handoff is finalized at a commit SHA.
- Full dependency bootstrap, product build, installer packaging, Pages composition, UI interaction, captures, and remote release publication require the other implementation lanes to be integrated first and remain unverified here.
- GitHub Actions deliberately runs no tests, lint, type checks, static analysis, coverage, accessibility checks, or screenshot checks. Those remain local responsibilities and do not gate release publication in the workflow.

## Next owner actions

1. Integrate the console, control-plane, site, and documentation lanes.
2. Replace unverified inventory evidence with exact merged paths and capture records.
3. Run the local checks, bootstrap, build, package, Pages composition, and built-artifact interaction at one pinned commit.
4. Publish from the default branch, verify the release and Pages output independently, then update this handoff with the exact commit and run links.
