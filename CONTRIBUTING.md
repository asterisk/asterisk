# Contributing to Ding PBX

Thank you for improving the project. Keep changes narrow, reviewable, and safe for people operating real telephony systems.

## Start from a reproducible environment

1. Run `download-dependencies.bat /s` on Windows. It installs the pinned user-scoped toolchain and exact lockfile dependencies.
2. Run `build.bat /s` for the runnable console.
3. Run relevant local checks for the files you changed. GitHub Actions intentionally does not run tests or lint.
4. For release-path changes, run `build-installer.bat /s` and inspect the reported unsigned Squirrel.Windows artifacts and SHA-256 values.

Do not work around bootstrap failures with an unpinned global runtime. Fix the committed bootstrap or manifest so a fresh machine receives the same result.

## Change expectations

- Preserve upstream Asterisk behavior unless the task explicitly changes it.
- Keep secrets, credentials, private configuration, call data, and machine-specific paths out of commits and issue content.
- Add or update focused local checks for changed behavior, including failure cases.
- Keep user-facing surfaces accessible, keyboard-operable, localized, responsive, and consistent with Material Design 3.
- Update affected documentation, roadmap items, handoff evidence, per-surface inventory rows, and design-parity records in the same change.
- Use the committed line counter when line-count evidence is needed: `node console/scripts/count-lines.mjs`.

## Commit and review

Use a concise subject and a body that explains behavior, cause, and verification. Never claim a check passed when it was not run. Avoid large generated or dependency trees; the bootstrap must fetch dependencies from their canonical pinned sources.

Security reports belong through the private process in `SECURITY.md`, not a public issue. Do not include an exploit, credential, personal data, or production configuration in a public report.
