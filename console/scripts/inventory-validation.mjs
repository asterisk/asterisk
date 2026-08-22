const requiredTemplateKeys = [
  'implementation', 'documentation', 'localization', 'localCheck', 'builtInteraction', 'capture',
];
const parityTemplateKeys = [
  'referenceRoute', 'builtRoute', 'referenceCapture', 'builtCapture', 'sideBySide', 'visualDiff', 'materialAudit',
];

function exactSet(actual, expected, label) {
  if (actual.length !== expected.length) throw new Error(`${label}: expected ${expected.length} entries, found ${actual.length}`);
  const unique = new Set(actual);
  if (unique.size !== actual.length) throw new Error(`${label}: duplicate identifier found`);
  for (const value of expected) if (!unique.has(value)) throw new Error(`${label}: missing exact identifier '${value}'`);
  for (const value of unique) if (!expected.includes(value)) throw new Error(`${label}: unexpected identifier '${value}'`);
}

function exactKeys(record, expected, label) {
  if (!record || typeof record !== 'object' || Array.isArray(record)) throw new Error(`${label}: object required`);
  exactSet(Object.keys(record), expected, label);
  for (const key of expected) {
    if (typeof record[key] !== 'string' || !record[key].includes('{id}')) throw new Error(`${label}.${key}: nonempty {id} template required`);
  }
}

export function validateSurfaceInventory(data, { allowUnverified = false } = {}) {
  if (data?.schemaVersion !== 1) throw new Error('surface inventory: schemaVersion 1 required');
  if (!Array.isArray(data.requiredFeatureIds) || data.requiredFeatureIds.length === 0) throw new Error('surface inventory: requiredFeatureIds must be nonempty');
  exactSet(data.requiredFeatureIds, data.requiredFeatureIds, 'requiredFeatureIds');
  if (!Array.isArray(data.surfaces)) throw new Error('surface inventory: surfaces array required');
  exactSet(data.surfaces.map((surface) => surface.id), ['windows-console', 'pages-site'], 'surface identifiers');
  for (const surface of data.surfaces) {
    exactKeys(surface.evidenceTemplates, requiredTemplateKeys, `${surface.id}.evidenceTemplates`);
    if (!Array.isArray(surface.features)) throw new Error(`${surface.id}: features array required`);
    exactSet(surface.features.map((feature) => feature.id), data.requiredFeatureIds, `${surface.id}.features`);
    for (const feature of surface.features) {
      exactSet(Object.keys(feature), ['id', 'status'], `${surface.id}.${feature.id} fields`);
      if (!['verified', 'unverified'].includes(feature.status)) throw new Error(`${surface.id}.${feature.id}: invalid status`);
      if (!allowUnverified && feature.status !== 'verified') throw new Error(`${surface.id}.${feature.id}: evidence remains unverified`);
    }
  }
  return { surfaces: data.surfaces.length, featuresPerSurface: data.requiredFeatureIds.length };
}

const destinationIds = [
  'dash','live','endpoints','trunks','trunkauth','canvas','ivr','queues',
  'voicemail','confbridge','moh','codecs','cdr','ami','modules','logger','security','cli',
  'memory','sync','skills','hub','vocab','ops','secrets',
  'servers','arcade','notifications','history','customise','appearance','about',
];
const transientStates = [
  'appearOpen','ceremonyOpen','ctxOpen','infoOpen','lockOpen','onboardOpen','paletteOpen','regexOpen',
  'renameOpen','subOpen','sureOpen','tabColourOpen','tabFilterOpen','toastOpen','tourOpen','unlockOpen','wizardOpen',
];
const exactRails = { pbx: 8, media: 4, data: 2, system: 4, agent: 7, app: 7 };
const exactBindings = {
  total: 265, click: 212, change: 10, input: 10, contextmenu: 9,
  dragstart: 4, dragover: 4, drop: 4, dragend: 4, mousedown: 5, mouseenter: 1, mouseleave: 1, mouseup: 1,
};

export function validateParityInventory(data, { allowUnverified = false } = {}) {
  if (data?.schemaVersion !== 1) throw new Error('design parity inventory: schemaVersion 1 required');
  if (data.sourceArchive?.sha256 !== '9A4284745A745C18A18B0A23D2A2F5851A79F9B6EFCBC5EE30EDCD69CEA2863F') throw new Error('design parity inventory: source archive SHA-256 drift');
  if (data.sourceArchive?.verification !== 'independent-authoritative-audit') throw new Error('design parity inventory: source verification label drift');
  exactKeys(data.evidenceTemplates, parityTemplateKeys, 'design parity evidenceTemplates');
  if (data.auditBaseline?.destinationCount !== 32) throw new Error('design parity inventory: destination count must be 32');
  exactSet(Object.keys(data.auditBaseline?.railCounts ?? {}), Object.keys(exactRails), 'rail identifiers');
  for (const [rail, count] of Object.entries(exactRails)) if (data.auditBaseline.railCounts[rail] !== count) throw new Error(`design parity inventory: rail '${rail}' count drift`);
  exactSet(Object.keys(data.auditBaseline?.declarativeBindings ?? {}), Object.keys(exactBindings), 'binding identifiers');
  for (const [event, count] of Object.entries(exactBindings)) if (data.auditBaseline.declarativeBindings[event] !== count) throw new Error(`design parity inventory: binding '${event}' count drift`);
  const bindingSum = Object.entries(exactBindings).filter(([event]) => event !== 'total').reduce((sum, [, count]) => sum + count, 0);
  if (bindingSum !== exactBindings.total) throw new Error('design parity validator: hard-coded binding arithmetic is invalid');
  if (data.auditBaseline.distinctExpressionCount !== 168) throw new Error('design parity inventory: distinct expression count drift');
  if (data.auditBaseline.controlCount !== 479) throw new Error('design parity inventory: control count drift');
  if (data.auditBaseline.transientStateFamilyCount !== 17) throw new Error('design parity inventory: transient-state count drift');
  if (!Array.isArray(data.destinations)) throw new Error('design parity inventory: destinations array required');
  exactSet(data.destinations.map((destination) => destination.id), destinationIds, 'destination identifiers');
  for (const destination of data.destinations) {
    exactSet(Object.keys(destination), ['rail', 'id', 'status'], `destination ${destination.id} fields`);
    if (!(destination.rail in exactRails)) throw new Error(`destination ${destination.id}: invalid rail '${destination.rail}'`);
    if (!['verified', 'unverified'].includes(destination.status)) throw new Error(`destination ${destination.id}: invalid status`);
    if (!allowUnverified && destination.status !== 'verified') throw new Error(`destination ${destination.id}: evidence remains unverified`);
  }
  for (const [rail, expected] of Object.entries(exactRails)) {
    const actual = data.destinations.filter((destination) => destination.rail === rail).length;
    if (actual !== expected) throw new Error(`destination rail '${rail}': expected ${expected}, found ${actual}`);
  }
  exactSet(data.transientStateFamilies ?? [], transientStates, 'transient-state identifiers');
  return { destinations: data.destinations.length, transientStates: data.transientStateFamilies.length };
}
