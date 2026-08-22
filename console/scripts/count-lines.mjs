#!/usr/bin/env node
import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, extname, resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..', '..');
const args = process.argv.slice(2);
const valueAfter = (flag) => {
  const index = args.indexOf(flag);
  return index >= 0 ? args[index + 1] : undefined;
};
const markdownPath = valueAfter('--markdown');
const jsonPath = valueAfter('--json');

const git = (...gitArgs) => execFileSync('git', gitArgs, {
  cwd: root,
  encoding: 'utf8',
  maxBuffer: 1024 * 1024 * 256,
  stdio: ['ignore', 'pipe', 'pipe'],
  windowsHide: true,
});

const tracked = git('ls-files', '-z').split('\0').filter(Boolean).sort();
const markupExtensions = new Set([
  '.css', '.scss', '.sass', '.less', '.html', '.htm', '.svg', '.md', '.mdx', '.adoc',
  '.txt', '.xml', '.json', '.jsonl', '.yaml', '.yml', '.toml', '.ini', '.conf', '.mustache',
]);
const sourceExtensions = new Set([
  '.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.m', '.mm', '.js', '.cjs', '.mjs',
  '.jsx', '.ts', '.tsx', '.py', '.rs', '.go', '.java', '.cs', '.sh', '.ps1', '.bat', '.cmd',
  '.sql', '.proto', '.cmake', '.mk', '.in', '.ac', '.am',
]);
const projectRoots = [
  '.github/', 'console/', 'control-plane/', 'site/', 'docs/', 'design/',
  'AGENTS.md', 'ROADMAP.md', 'HANDOFF.md', 'CONTRIBUTING.md', 'CODE_OF_CONDUCT.md',
  'build.bat', 'build-installer.bat', 'download-dependencies.bat', 'dependency-manifest.json',
];
const generatedSegments = ['/dist/', '/generated/', '/coverage/', '/release/line-count.'];
const testSegments = ['/test/', '/tests/', '.test.', '.spec.', '/__tests__/'];

function isProjectPath(path) {
  return projectRoots.some((rootPath) => path === rootPath || path.startsWith(rootPath));
}

function classify(path) {
  const normalized = `/${path.replaceAll('\\', '/')}`;
  if (!isProjectPath(path)) return 'vendor';
  if (generatedSegments.some((segment) => normalized.includes(segment))) return 'generated';
  if (testSegments.some((segment) => normalized.includes(segment))) return 'test';
  const extension = extname(path).toLowerCase();
  if (markupExtensions.has(extension)) return 'markup';
  if (sourceExtensions.has(extension) || !extension) return 'source';
  return 'markup';
}

function splitLines(buffer) {
  if (buffer.includes(0)) return null;
  const text = buffer.toString('utf8');
  if (text.includes('\uFFFD')) return null;
  if (text.length === 0) return [];
  const lines = text.replaceAll('\r\n', '\n').replaceAll('\r', '\n').split('\n');
  if (lines.at(-1) === '') lines.pop();
  return lines;
}

const automationCommits = new Set();
for (const record of git('log', '--format=%H%x1f%an%x1f%ae%x1f%B%x1e', '--all').split('\x1e')) {
  if (!record.trim()) continue;
  const [sha, author = '', email = '', body = ''] = record.trimStart().split('\x1f');
  if (
    author === 'Claude Fable 5' ||
    email === 'noreply@anthropic.com' ||
    /Co-Authored-By:\s*(?:Claude Fable 5|[^<]*(?:bot|automation))/i.test(body)
  ) automationCommits.add(sha);
}

function blameOwners(path, lineCount) {
  const owners = [];
  try {
    const output = git('blame', '--line-porcelain', 'HEAD', '--', path);
    for (const line of output.split('\n')) {
      const match = /^([0-9a-f]{40}) \d+ \d+(?: \d+)?$/.exec(line);
      if (match) owners.push(automationCommits.has(match[1]) ? 'agent' : 'human');
    }
  } catch {
    return Array(lineCount).fill('human');
  }
  if (owners.length !== lineCount) return Array(lineCount).fill('human');
  return owners;
}

const categoryOrder = ['source', 'test', 'markup', 'generated', 'vendor'];
const rows = Object.fromEntries(categoryOrder.map((category) => [category, {
  category, files: 0, total: 0, nonblank: 0, agent: 0, human: 0, generated: 0, external: 0,
}]));

for (const path of tracked) {
  let buffer;
  try { buffer = readFileSync(resolve(root, path)); }
  catch { continue; }
  const lines = splitLines(buffer);
  if (lines === null) continue;
  const category = classify(path);
  const row = rows[category];
  row.files += 1;
  row.total += lines.length;
  row.nonblank += lines.filter((line) => line.trim().length > 0).length;
  if (category === 'vendor') {
    row.external += lines.length;
  } else if (category === 'generated') {
    row.generated += lines.length;
  } else {
    for (const owner of blameOwners(path, lines.length)) row[owner] += 1;
  }
}

const project = categoryOrder.filter((category) => category !== 'vendor').reduce((sum, category) => {
  for (const key of ['files', 'total', 'nonblank', 'agent', 'human', 'generated', 'external']) sum[key] += rows[category][key];
  return sum;
}, { category: 'project total', files: 0, total: 0, nonblank: 0, agent: 0, human: 0, generated: 0, external: 0 });
const grand = categoryOrder.reduce((sum, category) => {
  for (const key of ['files', 'total', 'nonblank', 'agent', 'human', 'generated', 'external']) sum[key] += rows[category][key];
  return sum;
}, { category: 'grand total', files: 0, total: 0, nonblank: 0, agent: 0, human: 0, generated: 0, external: 0 });

for (const row of [...Object.values(rows), project, grand]) {
  const attributed = row.agent + row.human + row.generated + row.external;
  if (attributed !== row.total) throw new Error(`Attribution arithmetic mismatch for ${row.category}: ${attributed} != ${row.total}`);
}

const result = {
  schemaVersion: 1,
  commit: git('rev-parse', 'HEAD').trim(),
  command: 'node console/scripts/count-lines.mjs',
  exclusions: ['untracked files', 'binary files', 'dependency directories and build output not tracked by Git'],
  attributionRule: 'Surviving blamed lines are agent-written when the commit author is Claude Fable 5, uses noreply@anthropic.com, or has an automation Co-Authored-By trailer. Generated and inherited upstream lines are reported separately.',
  rows: categoryOrder.map((category) => rows[category]),
  project,
  grand,
};

const tableRows = [...result.rows, project, grand];
const markdown = [
  '| Category | Files | Total lines | Non-blank | Agent | Human | Generated | External/vendor |',
  '|---|---:|---:|---:|---:|---:|---:|---:|',
  ...tableRows.map((row) => `| ${row.category} | ${row.files} | ${row.total} | ${row.nonblank} | ${row.agent} | ${row.human} | ${row.generated} | ${row.external} |`),
  '',
  `Measured at commit \`${result.commit}\` with \`${result.command}\`.`,
  '',
  `Attribution: ${result.attributionRule}`,
  '',
  `Excluded: ${result.exclusions.join('; ')}.`,
].join('\n');

if (markdownPath) {
  const destination = resolve(root, markdownPath);
  mkdirSync(dirname(destination), { recursive: true });
  writeFileSync(destination, `${markdown}\n`, 'utf8');
}
if (jsonPath) {
  const destination = resolve(root, jsonPath);
  mkdirSync(dirname(destination), { recursive: true });
  writeFileSync(destination, `${JSON.stringify(result, null, 2)}\n`, 'utf8');
}
process.stdout.write(`${markdown}\n`);
