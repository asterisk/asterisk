#!/usr/bin/env node
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { validateSurfaceInventory } from './inventory-validation.mjs';

const root = resolve(import.meta.dirname, '..', '..');
const source = JSON.parse(readFileSync(resolve(root, 'console/inventories/surface-completeness.json'), 'utf8'));
const clone = () => structuredClone(source);

function mustFail(name, mutate) {
  const candidate = clone();
  mutate(candidate);
  try { validateSurfaceInventory(candidate, { allowUnverified: true }); }
  catch (error) { console.log(`RED: ${name}: ${error.message}`); return; }
  throw new Error(`${name}: deliberate break stayed green`);
}

validateSurfaceInventory(source, { allowUnverified: true });
mustFail('remove a required feature declaration', (data) => data.requiredFeatureIds.pop());
mustFail('remove one surface feature row', (data) => data.surfaces[0].features.pop());
mustFail('rename a feature with a containing suffix', (data) => { data.surfaces[0].features[0].id += '-renamed'; });
mustFail('remove one evidence template', (data) => { delete data.surfaces[1].evidenceTemplates.capture; });
validateSurfaceInventory(source, { allowUnverified: true });
console.log('GREEN: restored surface completeness inventory passed exact-boundary validation.');
