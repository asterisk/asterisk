# Ding PBX delivery roadmap

## Delivery foundation

- [x] Add a pinned, digest-verified, user-scoped Windows dependency bootstrap.
- [x] Add touchless and interactive root build entry points.
- [x] Add unsigned Squirrel.Windows package verification for `Setup.exe`, `RELEASES`, and full packages.
- [x] Replace inherited test and administration workflows with one build, package, release, and Pages workflow.
- [x] Add unique release tags, workflow timing, SHA-256 records, safe failure artifacts, and runner fallback.
- [x] Add a reproducible committed line counter with project, generated, and inherited-source attribution.

## Evidence and completeness

- [x] Record the independent design audit's source hash and exact aggregate counts.
- [x] Add fail-closed schema and completeness validators.
- [x] Add deliberate red-then-green negative regression scripts.
- [ ] Map all 32 audited destinations to final reference routes, product routes, and built-artifact captures after integration.
- [ ] Replace every unverified per-surface inventory field with merged implementation, documentation, localization, local-check, interaction, and capture evidence.
- [ ] Run the built Windows console through the approved headless interaction route and record genuine design-parity evidence.

## Release readiness

- [ ] Verify `download-dependencies.bat /s` from a clean user-scoped toolchain cache.
- [ ] Verify `build.bat /s` at the merged candidate commit.
- [ ] Verify `build-installer.bat /s` produces an unsigned installable Squirrel.Windows set.
- [ ] Verify the static Pages output includes `console/site/dist/build-manifest.json` and deploys without runtime asset fetches.
- [ ] Publish and independently verify the first unique non-draft Ding PBX Console release and downloadable assets.
