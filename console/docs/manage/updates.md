# Updates

## Behavior

Updates checks a configured HTTPS feed at startup and on a bounded schedule, validates feed metadata and package hashes, downloads in the background, and shows a persistent ready-to-restart banner. Restart occurs only after user choice.

## Configuration

The unsigned feed, check interval, automatic download, quiet hours, and release channel are visible. The current version, available version, release notes, unsigned-artifact warning, and manual check action remain accessible.

## Failure modes and security

Offline, invalid metadata, hash mismatch, corrupt asset, cancellation, insufficient storage, rollback, and unsaved work are separate states. Code signing is not used or claimed; transport and hashes provide the stated integrity evidence.

## Verification

Exercise no-update, available, downloading, ready, restart, later, offline, invalid feed, hash mismatch, corruption, cancellation, rollback, unsaved work, and exact release-manifest readback.

## Suggested articles

[Status](../overview/status.md), [Backups](backups.md), and [Security](../team-calling/security.md).
