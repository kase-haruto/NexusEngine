import { readFile, writeFile, mkdir, cp, stat, readdir } from 'node:fs/promises';
import { createReadStream } from 'node:fs';
import { createHash } from 'node:crypto';
import { resolve, dirname, relative, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
import { marked } from 'marked';
import sanitizeHtml from 'sanitize-html';
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const repository = resolve(root, '../..');
const releaseRoot = resolve(process.env.NEXUS_RELEASE_ROOT || resolve(repository, 'Generated/EngineReleases'));
const catalogPath = resolve(process.env.NEXUS_CATALOG || resolve(releaseRoot, 'releases.local.json'));
const readJson = async p => JSON.parse((await readFile(p, 'utf8')).replace(/^\uFEFF/, ''));
const hash = async p => { const h = createHash('sha256'); for await (const chunk of createReadStream(p)) h.update(chunk); return h.digest('hex'); };
export function within(root, value) {
  if (typeof value !== 'string' || !value || value.includes(':')) throw new Error('Expected a local relative package path.');
  const path = resolve(root, value); const rel = relative(root, path);
  if (rel === '..' || rel.startsWith('..' + sep) || resolve(path) === resolve(root)) throw new Error('Package path escapes release root.');
  return path;
}
const escapeHtml = value => value.replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
export function renderDoc(markdown) {
  return sanitizeHtml(marked.parse(markdown), {
    allowedTags: sanitizeHtml.defaults.allowedTags.concat(['h1','h2','img']),
    allowedAttributes: { a: ['href','title'], img: ['src','alt'], code: ['class'] },
    allowedSchemes: ['https','http','mailto'],
    transformTags: { a: (tag, attrs) => ({ tagName: tag, attribs: { ...attrs, href: (attrs.href || '').replace(/\.md(?=#|$)/, '.html') } }) }
  });
}
export function publishedDownload(item, assets) {
  const asset = assets[item.key];
  if (!asset) return { ...item, available: false, url: '' };
  const url = new URL(asset.url);
  if (url.protocol !== 'https:' || url.hostname !== 'github.com' || url.username || url.password || url.port || url.search || url.hash || !/^\/kase-haruto\/NexusEngine\/releases\/download\/distribution-[a-f0-9]{12}\/[a-zA-Z0-9._-]+$/.test(url.pathname) || asset.sha256 !== item.sha256 || asset.bytes !== item.bytes) throw new Error('Published GitHub asset does not match verified package.');
  return { ...item, available: true, url: url.href };
}
async function build() {
  let published = {};
  try { published = await readJson(resolve(root, 'published-assets.json')); } catch (error) { if (error.code !== 'ENOENT') throw error; }
  const input = await readJson(catalogPath);
  if (input.schemaVersion !== 1 || !Array.isArray(input.releases) || !input.releases.length) throw new Error('Invalid release catalog.');
  const releases = [], uploads = [], keys = new Set();
  for (const r of input.releases) {
    if (!/^[a-zA-Z0-9][a-zA-Z0-9._-]{0,99}$/.test(r.id) || !/^\d+\.\d+\.\d+(?:-[a-zA-Z0-9.-]+)?$/.test(r.version) || !['Debug','Develop','Release'].includes(r.configuration) || typeof r.changes !== 'string') throw new Error('Invalid release identity.');
    const file = within(releaseRoot, r.url);
    const bytes = (await stat(file)).size;
    const sha256 = await hash(file);
    if (bytes !== r.bytes || sha256 !== r.sha256.toLowerCase()) throw new Error(`Package verification failed: ${r.version}`);
    const key = `engines/${r.id}-${r.version}-${r.configuration}.zip`;
    if (keys.has(key)) throw new Error('Duplicate release key.'); keys.add(key);
    releases.push({ id: r.id, version: r.version, configuration: r.configuration, changes: r.changes, sha256, bytes, key });
    uploads.push({ key, file, bytes, sha256 });
  }
  releases.sort((a,b) => b.version.localeCompare(a.version, undefined, { numeric: true }));
  let launcher = null;
  // A public installer must be rebuilt with the actual HTTPS catalog; never publish a local preview installer.
  if (process.env.NEXUS_LAUNCHER_SETUP) {
    const file = resolve(process.env.NEXUS_LAUNCHER_SETUP);
    const settings = await readJson(resolve(dirname(file), '../Launcher/launcher.json'));
    const origin = new URL(process.env.NEXUS_SITE_ORIGIN);
    if (origin.protocol !== 'https:' || origin.username || origin.password || origin.pathname !== '/' || origin.search || origin.hash || settings.catalogUrl !== new URL('/releases.json', origin).href || settings.catalogFile) throw new Error('Installer must use the production HTTPS catalog without local paths.');
    const sha256 = await hash(file), bytes = (await stat(file)).size;
    launcher = { key: `launcher/${sha256}/launcherSetup.exe`, bytes, sha256 };
    uploads.push({ ...launcher, file });
  }
  else {
    const known = Object.entries(published).find(([key]) => /^launcher\/[a-f0-9]{64}\/launcherSetup\.exe$/.test(key));
    if (known) launcher = { key: known[0], bytes: known[1].bytes, sha256: known[1].sha256 };
  }
  await mkdir(resolve(root, 'dist/data'), { recursive: true });
  await cp(resolve(root, 'public'), resolve(root, 'dist'), { recursive: true });
  const docsRoot = resolve(repository, 'Project/Docs');
  const aliases = { 'HybridProjectREADME.md': 'project', 'GameApiReference.md': 'api', 'MonoRuntimeREADME.md': 'mono', 'GamepadInputREADME.md': 'gamepad' };
  await mkdir(resolve(root, 'dist/docs'), { recursive: true });
  for (const name of await readdir(docsRoot)) {
    if (!name.endsWith('.md')) continue;
    const markdown = await readFile(resolve(docsRoot, name), 'utf8');
    const title = escapeHtml(markdown.match(/^#\s+(.+)/m)?.[1] || name);
    const html = `<!doctype html><html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>${title} | NexusEngine</title><link rel="stylesheet" href="/style.css"><link rel="icon" href="/mark.svg"></head><body><header><a class="brand" href="/"><img src="/mark.svg" width="32" height="32" alt="">NEXUS<span>ENGINE</span></a><a href="/#learn">ドキュメント一覧へ ↗</a></header><main class="wrap doc-page"><p class="doc-notice">エンジンの開発ドキュメント / 現行ソースの資料です。公開SDKとの対応は各バージョンの変更点もご確認ください。</p><article class="doc-content">${renderDoc(markdown)}</article></main></body></html>`;
    await writeFile(resolve(root, 'dist/docs', name.replace(/\.md$/, '.html')), html);
    if (aliases[name]) await writeFile(resolve(root, 'dist/docs', aliases[name] + '.html'), html);
  }
  const publicReleases = releases.map(item => publishedDownload(item, published));
  const publicLauncher = launcher ? publishedDownload(launcher, published) : null;
  await writeFile(resolve(root, 'dist/data/catalog.json'), JSON.stringify({ schemaVersion: 1, releases: publicReleases, launcher: publicLauncher }, null, 2));
  await writeFile(resolve(root, 'dist/releases.json'), JSON.stringify({ schemaVersion: 1, releases: publicReleases.filter(r => r.available).map(({ key, available, ...release }) => release) }, null, 2));
  await writeFile(resolve(root, 'upload-manifest.json'), JSON.stringify(uploads, null, 2));
  console.log(`Built ${releases.length} verified releases. Installer: ${launcher ? 'ready' : 'not published'}.`);
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) await build();
