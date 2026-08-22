#!/usr/bin/env node
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { validateSurfaceInventory, validateParityInventory } from './inventory-validation.mjs';

const root = resolve(import.meta.dirname, '..', '..');
const allowUnverified = process.argv.includes('--allow-unverified');
const readJson = (relativePath) => JSON.parse(readFileSync(resolve(root, relativePath), 'utf8'));

try {
  const surface = validateSurfaceInventory(readJson('console/inventories/surface-completeness.json'), { allowUnverified });
  const parity = validateParityInventory(readJson('console/inventories/design-parity.json'), { allowUnverified });
  console.log(`PASS: ${surface.surfaces} surfaces x ${surface.featuresPerSurface} exact feature rows; ${parity.destinations} destinations; ${parity.transientStates} transient-state families.`);
} catch (error) {
  console.error(`FAIL: ${error.message}`);
  process.exitCode = 1;
}
