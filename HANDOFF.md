# Ding PBX delivery handoff

## Scope

This handoff covers the integrated Ding PBX Console desktop application, bounded PBX control plane, GitHub Pages documentation application, repository delivery automation, line counting, completeness and design-parity inventories, contributor guidance, and release evidence contracts.

## Implemented

- Pinned Node.js `22.23.2` for Windows x64 from the official Node.js release service with SHA-256 `1177b4137ba5adaa56354ae40f1080c7450e8ae09cecb47da459d1c52ac99f97`.
- Added silent modes `/s`, `--silent`, and `SILENT=1`; user-scoped extraction under local application data; digest verification; reproducible `npm ci`; phase timing; and actionable failure output.
- Added runnable-build and installer-build entry points. Packaging clears signing inputs, invokes the product's Squirrel.Windows script, checks required files and the `RELEASES` index, verifies `Setup.exe` is `NotSigned`, and prints file sizes and SHA-256 values.
- Replaced inherited workflows with one Windows-only build/package/Pages/release workflow. It probes repository self-hosted runner availability and otherwise uses pinned `windows-2025`. The observed repository inventory contained zero self-hosted runners; organization inventory was unavailable with HTTP 403, so the workflow does not claim organization-runner evidence.
- Added one unique monotonic `ding-pbx-console-v0.0.<run>-r<attempt>` release per successful push or manual run, exact target verification, non-draft verification, required-asset verification, workflow start/completion/duration, SHA-256 records, and safe always-upload evidence.
- Added a committed line counter that separates project source, tests, markup, generated output, and inherited/vendor source, with surviving-line authorship for project files.
- Implemented the 32 audited destinations across six navigation rails in the packaged desktop application, with tabbed navigation, searchable menus and lists, an anchored regex builder, command palette, guided flows, appearance controls, non-blocking notifications, and guarded destructive previews.
- Implemented a bounded process control plane with no shell execution, allowlisted commands, WSL discovery, project-labelled container discovery, scoped SSH trust, pinned Asterisk provisioning plans, staged configuration validation, backups, post-write reads, and rollback.
- Implemented a dependency-free static documentation application containing the same 32 destination identifiers, 32 feature articles, local settings and search behavior, deterministic output, an Open Graph graphic, and no runtime asset fetches.

## Independent audit baseline

- Original design archive SHA-256: `9A4284745A745C18A18B0A23D2A2F5851A79F9B6EFCBC5EE30EDCD69CEA2863F`.
- Destinations: `32`.
- Navigation rail counts: `8 / 4 / 2 / 4 / 7 / 7`.
- Declarative bindings: `265` total: `212` click, `10` change, `10` input, `9` context menu, `4` each drag start/over/drop/end, `5` mouse down, and `1` each mouse enter/leave/up.
- Distinct expressions: `168`.
- Controls: `479`.
- Transient-state families: `17`.

## Verification state

- Candidate commit `5e7cc508d470b022c96d4008dc6b0927f5748d6f` passed 49 local tests: 6 desktop UI, 34 control-plane, and 9 static-site tests. Both inventory negative regressions were observed red after a deliberate removal and green after restoration.
- A cold user-scoped dependency bootstrap and `build.bat /s` completed successfully. The exact candidate produced an intentionally unsigned Squirrel.Windows set: `Ding-PBX-Console-Setup.exe` is 294,705,152 bytes with SHA-256 `714767a464b91dc3c1f763a763fd6c855188b10771a84bd76b138ddbed23568b`; the full package is 294,208,940 bytes with SHA-256 `cff93c46d0f05e0ddea11835b24328dcc6978e50438e866763e64a87f7476b4b`.
- The packaged application ran on an isolated hidden Windows desktop with one exact renderer target. Its real preload bridge discovered the installed `Ubuntu` WSL distribution from **App > Deploy & servers > Discover local targets**. The inspected capture is `console/release/captures/windows-console/servers.png`; its SHA-256 is `f3621c0c622fba580cc2ad9908a631eac34a73e4c8f90ab8ddca0d857a951aa6`.
- The static-site builder produced 43 deterministic files including `console/site/dist/build-manifest.json`; the site test rejects runtime network fetches.
- GitHub Actions deliberately runs no tests, lint, type checks, static analysis, coverage, accessibility checks, or screenshot checks. Those remain local responsibilities and do not gate release publication in the workflow.
- Remote release and GitHub Pages publication are pending the default-branch push and must be recorded below rather than predicted.

## Next owner actions

1. Publish the integrated candidate from the default branch and verify the workflow, release assets, and GitHub Pages response independently.
2. Continue replacing unverified inventory evidence with exact built-interaction and capture records; the current task proves the server-discovery route, not all 32 parity tuples or every universal feature.
3. Exercise an explicitly approved target-specific write plan against a disposable PBX target before describing any configuration mutation as production-verified.
