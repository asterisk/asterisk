import { createHash } from 'node:crypto';
import { copyFile, mkdir, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = dirname(fileURLToPath(import.meta.url));
const docs = resolve(root, '..', 'docs');
const output = join(root, 'dist');
const assets = ['index.html', 'styles.css', 'app.js'];
const socialPreview = resolve(root, '..', '..', 'social-preview.png');

if (process.argv.includes('--clean')) {
  await rm(output, { recursive: true, force: true });
  console.log(`Removed generated output ${output}`);
  process.exit(0);
}

await rm(output, { recursive: true, force: true });
await mkdir(output, { recursive: true });

for (const asset of assets) {
  let content = await readFile(join(root, asset));
  if (asset === 'index.html') {
    content = Buffer.from(content.toString('utf8').replaceAll('../docs/', 'docs/').replaceAll('.md"', '.html"'));
  }
  await writeFile(join(output, asset), content);
}
await copyFile(socialPreview, join(output, 'social-preview.png'));

function escapeHtml(value) {
  return value.replace(/[&<>'"]/g, character => ({ '&':'&amp;', '<':'&lt;', '>':'&gt;', "'":'&#39;', '"':'&quot;' })[character]);
}
function inlineMarkdown(value) {
  return escapeHtml(value)
    .replace(/\[([^\]]+)\]\(([^)]+)\)/g, (_match, label, href) => `<a href="${href.replace(/\.md$/, '.html')}">${label}</a>`)
    .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
    .replace(/`([^`]+)`/g, '<code>$1</code>');
}
function renderMarkdown(markdown) {
  const outputLines=[]; let listOpen=false;
  for(const rawLine of markdown.replaceAll('\r\n','\n').split('\n')) {
    const line=rawLine.trim();
    if(line.startsWith('- ')){if(!listOpen){outputLines.push('<ul>');listOpen=true}outputLines.push(`<li>${inlineMarkdown(line.slice(2))}</li>`);continue}
    if(listOpen){outputLines.push('</ul>');listOpen=false}
    if(!line)continue;
    const heading=line.match(/^(#{1,6})\s+(.+)$/);if(heading){const level=heading[1].length;outputLines.push(`<h${level}>${inlineMarkdown(heading[2])}</h${level}>`)}else outputLines.push(`<p>${inlineMarkdown(line)}</p>`);
  }
  if(listOpen)outputLines.push('</ul>');
  return outputLines.join('\n');
}
async function composeDocs(sourceRelative='') {
  for(const entry of await readdir(join(docs,sourceRelative),{withFileTypes:true})) {
    const child=join(sourceRelative,entry.name);
    if(entry.isDirectory()){await composeDocs(child);continue}
    if(!entry.name.endsWith('.md'))continue;
    const markdown=await readFile(join(docs,child),'utf8'), title=markdown.match(/^#\s+(.+)$/m)?.[1]||'Ding PBX Console documentation';
    const htmlRelative=child.replace(/\.md$/,'.html'), destination=join(output,'docs',htmlRelative), depth=htmlRelative.split(/[\\/]/).length;
    const back='../'.repeat(depth), page=`<!doctype html>\n<html lang="en" data-theme="dark"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="description" content="Ding PBX Console feature documentation."><title>${escapeHtml(title)} · Ding PBX Console</title><link rel="stylesheet" href="${back}styles.css"></head><body><a class="skip-link" href="#article">Skip to article</a><main id="article" class="documentation-page"><nav aria-label="Documentation breadcrumb"><a href="${back}index.html">Ding PBX Console</a> · <a href="${back}docs/README.html">Documentation</a></nav><article>${renderMarkdown(markdown)}</article><footer><p>This documentation website is not the installed desktop application and is not a PBX runtime.</p></footer></main></body></html>\n`;
    await mkdir(dirname(destination),{recursive:true});await writeFile(destination,page,'utf8');
  }
}
await composeDocs();

const files = [];
async function record(relative) {
  const content = await readFile(join(output, relative));
  files.push({ path: relative.replaceAll('\\', '/'), bytes: content.length, sha256: createHash('sha256').update(content).digest('hex') });
}
async function walk(relative) {
  for (const entry of await readdir(join(output, relative), { withFileTypes: true })) {
    const child = join(relative, entry.name);
    if (entry.isDirectory()) await walk(child);
    else if (entry.name !== 'build-manifest.json') await record(child);
  }
}
await walk('.');

const manifest = {
  schemaVersion: 1,
  generatedBy: 'node console/site/build.mjs',
  networkFetches: 0,
  outputFiles: files.sort((a, b) => a.path.localeCompare(b.path))
};
await writeFile(join(output, 'build-manifest.json'), `${JSON.stringify(manifest, null, 2)}\n`, 'utf8');
console.log(`Composed ${output}`);
console.log(`Output files: ${files.length}; runtime network fetches: 0`);
