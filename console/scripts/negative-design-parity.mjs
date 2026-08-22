#!/usr/bin/env node
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { validateParityInventory } from './inventory-validation.mjs';

const root = resolve(import.meta.dirname, '..', '..');
const source = JSON.parse(readFileSync(resolve(root, 'console/inventories/design-parity.json'), 'utf8'));
const clone = () => structuredClone(source);

function mustFail(name, mutate) {
  const candidate = clone();
  mutate(candidate);
  try { validateParityInventory(candidate, { allowUnverified: true }); }
  catch (error) { console.log(`RED: ${name}: ${error.message}`); return; }
  throw new Error(`${name}: deliberate break stayed green`);
}

validateParityInventory(source, { allowUnverified: true });
mustFail('change the source archive digest', (data) => { data.sourceArchive.sha256 = `0${data.sourceArchive.sha256.slice(1)}`; });
mustFail('remove one exact destination', (data) => { data.destinations = data.destinations.filter(({ id }) => id !== 'dash'); });
mustFail('rename a destination with a containing suffix', (data) => { data.destinations[0].id = 'dash-renamed'; });
mustFail('change one rail count', (data) => { data.auditBaseline.railCounts.pbx = 7; });
mustFail('remove one binding event', (data) => { delete data.auditBaseline.declarativeBindings.mouseup; });
mustFail('remove one transient-state family', (data) => { data.transientStateFamilies.pop(); });
mustFail('remove one visual evidence template', (data) => { delete data.evidenceTemplates.visualDiff; });
validateParityInventory(source, { allowUnverified: true });
console.log('GREEN: restored design parity inventory passed exact-boundary validation.');
