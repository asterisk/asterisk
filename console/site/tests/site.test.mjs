import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { readFile, readdir, stat } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const repo = resolve(root, '..', '..');
const html = await readFile(join(root, 'index.html'), 'utf8');
const css = await readFile(join(root, 'styles.css'), 'utf8');
const js = await readFile(join(root, 'app.js'), 'utf8');

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

test('declares responsive and Open Graph metadata', () => {
  assert.match(html, /<meta name="viewport"/);
  for (const key of ['og:title','og:description','og:url','og:type','og:site_name','og:image','og:image:width','og:image:height','og:image:alt']) assert.match(html, new RegExp(`property="${key}"`));
  assert.match(html, /twitter:card" content="summary_large_image"/);
});
test('states the static site boundary and honest unavailable installer', () => {
  assert.match(html, /not the installed desktop application/i);
  assert.match(html, /not a PBX runtime/i);
  assert.match(html, /Windows installer unavailable/);
  assert.match(html, /No verified release manifest exists yet/);
});
test('contains exactly 32 destination definitions in six declared groups', () => {
  const block = js.match(/const DESTINATIONS = \[([\s\S]*?)\n  \];/)[1];
  assert.equal((block.match(/\{id:/g) || []).length, 32);
  const counts = [...block.matchAll(/group:'([^']+)'/g)].reduce((map, match) => map.set(match[1], (map.get(match[1]) || 0) + 1), new Map());
  assert.deepEqual([...counts.values()], [8,4,2,4,7,7]);
  assert.deepEqual([...block.matchAll(/\{id:'([^']+)'/g)].map(match => match[1]), [
    'dash','live','endpoints','trunks','trunkauth','canvas','ivr','queues',
    'voicemail','confbridge','moh','codecs','cdr','ami','modules','logger','security','cli',
    'memory','sync','skills','hub','vocab','ops','secrets',
    'servers','arcade','notifications','history','customise','appearance','about',
  ]);
});
test('provides 32 complete categorized articles with valid local links', async () => {
  const docsRoot=resolve(root,'..','docs'), categories=['overview','people-devices','connectivity','call-flow','team-calling','manage'];
  const articles=[];
  for(const category of categories)for(const name of await readdir(join(docsRoot,category)))if(name.endsWith('.md')&&name!=='README.md')articles.push(join(docsRoot,category,name));
  assert.equal(articles.length,32);
  for(const article of articles){const content=await readFile(article,'utf8');for(const heading of ['## Behavior','## Configuration','## Failure modes','## Verification','## Suggested articles'])assert.match(content,new RegExp(heading));for(const match of content.matchAll(/\]\(([^)]+\.md)\)/g)){const target=resolve(dirname(article),match[1]);assert.ok((await stat(target)).isFile(),`${article} -> ${match[1]}`)}}
});
test('exposes keyboard, tab, regex, and local settings interactions', () => {
  assert.match(html, /role="tablist"/); assert.match(html, /id="command-palette"/); assert.match(js, /e\.ctrlKey&&e\.shiftKey/);
  assert.ok((html.match(/class="regex-trigger"/g) || []).length >= 10);
  for (const id of ['language-mode','english-funny','cantonese-funny','vocabulary-file','attention-settings','schedule-enabled','logo-file','notification-history']) assert.match(html, new RegExp(`id="${id}"`));
});
test('has accessible names and reduced motion support', () => {
  assert.match(html, /class="skip-link"/); assert.match(html, /aria-live="polite"/); assert.match(html, /aria-label="Notifications"/);
  assert.match(css, /prefers-reduced-motion:reduce/); assert.match(css, /min-width:320px/); assert.match(css, /:focus-visible/);
});
test('uses no runtime CDN, analytics, or remote script and stylesheet assets', () => {
  assert.doesNotMatch(html, /(?:src|href)="https?:\/\//i);
  assert.doesNotMatch(html, /google-analytics|googletagmanager|unpkg|jsdelivr|cdnjs/i);
  assert.doesNotMatch(css, /@import|url\(\s*['"]?https?:/i);
});
test('documents local-only validation and redacted export boundaries', () => {
  assert.match(js, /file\.size>65536/); assert.match(js, /parsed\.version!==1/); assert.match(js, /Duplicate keys are not accepted/);
  assert.match(js, /personalVocabulary:'omitted'/); assert.match(html, /No data leaves this browser/);
});
test('build composes deterministic local output without fetches', async () => {
  execFileSync(process.execPath, [join(root, 'build.mjs')], { cwd: repo, stdio: 'pipe' });
  const manifest = JSON.parse(await readFile(join(root, 'dist', 'build-manifest.json'), 'utf8'));
  assert.equal(manifest.networkFetches, 0); assert.equal(manifest.outputFiles.length, 43);
  assert.ok(manifest.outputFiles.some(file => file.path === 'social-preview.png'));
  assert.ok((await stat(join(root, 'dist', 'docs', 'README.html'))).isFile());
  const built = await readFile(join(root, 'dist', 'index.html'), 'utf8');
  assert.doesNotMatch(built, /\.\.\/docs\//); assert.match(built, /href="docs\/README\.html"/);
  const article=await readFile(join(root,'dist','docs','overview','dashboard.html'),'utf8');assert.match(article,/<h1>Dashboard<\/h1>/);assert.doesNotMatch(article,/\.md"/);
});

let passed = 0;
for (const [name, fn] of tests) {
  try { await fn(); console.log(`PASS ${name}`); passed += 1; }
  catch (error) { console.error(`FAIL ${name}`); console.error(error.stack || error); process.exitCode = 1; }
}
console.log(`${passed}/${tests.length} tests passed`);
if (passed !== tests.length) process.exitCode = 1;
