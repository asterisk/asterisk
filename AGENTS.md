# Repository agent guidance

This file is a public-safe repository mirror. The canonical shared instructions remain external to this repository; update that source first, then refresh this mirror without copying private infrastructure, credentials, machine paths, host details, or conversational vocabulary.

## Scope and product boundaries

- Treat `console/` as the Ding PBX Console product, `control-plane/` as its local service boundary, and `console/site/` as the static documentation and download surface.
- Preserve the inherited Asterisk source unless the current task explicitly requires a telephony-core change.
- Keep user-facing implementation aligned with Material Design 3, accessibility, local-first privacy, responsive layouts, complete localization, and documented failure states.
- Do not edit any checked-in original design-reference source. Render it as data through the dedicated design-reference evidence path.

## Build and release discipline

- `download-dependencies.bat /s` installs the exact user-scoped toolchain and project dependencies from `dependency-manifest.json` and `console/package-lock.json`.
- `build.bat /s` builds the runnable console. `build-installer.bat /s` builds and verifies the unsigned Squirrel.Windows distribution.
- Code signing is permanently prohibited. Do not request, discover, create, store, or use signing credentials. Release notes must state that Windows may show unknown-publisher or SmartScreen warnings.
- GitHub Actions builds, packages, publishes Pages, and creates releases. It does not run tests, lint, type checks, static analysis, coverage, screenshots, or accessibility checks.
- Run relevant checks locally before committing. Never describe an unrun check as passed.
- Every release must contain one `Setup.exe`, `RELEASES`, at least one full `.nupkg`, generated delta packages when available, SHA-256 evidence, the reproducible line-count table, exact workflow timing, and a unique immutable `ding-pbx-console-*` tag.

## Security and privacy

- Never commit secrets, credentials, private keys, local machine paths, user data, captured call content, or sensitive PBX configuration.
- Validate all external input, keep network requests bounded, avoid command concatenation, and make destructive actions reviewable and explicit.
- Store credentials only in the operating-system credential store. Keep logs, exports, captures, and release evidence redacted.
- Prefer local, bundled, offline-capable dependencies. Disabled integrations must state the exact missing dependency or permission rather than silently failing.

## Repository records

- Keep `ROADMAP.md` as a factual checklist and `HANDOFF.md` as evidence, not intent.
- Update the per-surface inventory and design-parity inventory whenever a surface, feature, route, proof, or capture changes.
- Run the inventory validators and their negative regressions locally. A guard is trusted only after the deliberate break turns red and restoration turns green.
- Generate line counts only with `node console/scripts/count-lines.mjs`; do not replace the committed method with an ad hoc count.

## Git authorship

- Configure repository-local author and committer identity as `Claude Fable 5 <noreply@anthropic.com>` for agent-authored commits.
- Commit messages use a concise English subject, a playful Hong Kong-style Cantonese explanation in the body, and exactly one trailer:

  `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`
- Preserve unrelated work. Never force-push, rewrite, discard, or delete another task's changes.
