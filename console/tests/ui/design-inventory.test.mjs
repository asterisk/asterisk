import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const root = new URL('../../', import.meta.url);
const read = path => readFile(new URL(path, root), 'utf8');
const expectedDestinations = [
  'dash','live','endpoints','trunks','trunkauth','canvas','ivr','queues',
  'voicemail','confbridge','moh','codecs','cdr','ami','modules','logger','security','cli',
  'memory','sync','skills','hub','vocab','ops','secrets',
  'servers','arcade','notifications','history','customise','appearance','about',
];

function validateInventory(inventory) {
  assert.equal(inventory.schemaVersion, 1);
  assert.equal(inventory.source.sha256, '9A4284745A745C18A18B0A23D2A2F5851A79F9B6EFCBC5EE30EDCD69CEA2863F');
  assert.deepEqual(inventory.rails.map(r => r.destinations.length), [8,4,2,4,7,7]);
  assert.deepEqual(inventory.rails.flatMap(r => r.destinations), expectedDestinations);
  assert.deepEqual(inventory.auditBaseline, {
    bindings:{total:265,primary:233,reusableControl:32}, distinctExpressions:168,
    controlDeclarations:479, controlIds:467, openStateFamilies:17, onboardingSteps:5,
    tourSteps:5, confirmationGames:8, provisioningLogEntries:10, canvas:{nodes:6,edges:5},
  });
}

test('pins every authoritative design destination and audit count', async () => {
  validateInventory(JSON.parse(await read('design/inventory.json')));
});

test('negative regression: a removed destination turns the inventory check red', async () => {
  const inventory = JSON.parse(await read('design/inventory.json'));
  inventory.rails[0].destinations = inventory.rails[0].destinations.filter(id => id !== 'endpoints');
  assert.throws(() => validateInventory(inventory));
});

test('negative regression: a changed audit baseline turns the inventory check red', async () => {
  const inventory = JSON.parse(await read('design/inventory.json'));
  inventory.auditBaseline.bindings.total = 264;
  assert.throws(() => validateInventory(inventory));
});

test('renderer implements navigation, overlays, and the missing palette shortcut', async () => {
  const source = await read('app/renderer/src/App.tsx');
  for (const exact of [
    "e.ctrlKey&&e.shiftKey&&e.key.toLowerCase()==='f'",
    'Search current tab strip', 'Search tab groups', 'Master destination search',
    'Regex builder', 'Guided wizard', 'Appearance editor', 'Lock this element…',
    'Review consequential action', 'Notification centre',
  ]) assert.ok(source.includes(exact), `missing exact implementation marker: ${exact}`);
});

test('public source contains no runtime CDN or private source branding', async () => {
  const files = ['app/renderer/src/App.tsx','app/renderer/src/catalog.ts','app/renderer/src/styles.css','app/renderer/index.html'];
  const text = (await Promise.all(files.map(read))).join('\n');
  for (const forbidden of ['fonts.googleapis.com','fonts.gstatic.com','unpkg.com','Asterisk Console','support.js']) {
    assert.equal(text.includes(forbidden), false, `public renderer leaked forbidden source text: ${forbidden}`);
  }
});

test('preload exposes a typed, bounded control-plane boundary', async () => {
  const preload = await read('app/electron/preload.cjs');
  const main = await read('app/electron/main.ts');
  assert.match(preload, /contextBridge\.exposeInMainWorld\('dingDesktop', api\)/);
  assert.match(preload, /ipcRenderer\.invoke\('control-plane:request', request\)/);
  assert.match(main, /discoverWslDistributions/);
  assert.match(main, /discoverLocalDocker\('ding-pbx-console'\)/);
  assert.match(main, /TARGET_NOT_DISCOVERED/);
  assert.equal(main.includes('exec('), false);
  assert.equal(main.includes('spawn('), false);
});
