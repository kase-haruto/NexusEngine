import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
const origin = process.env.NEXUS_SITE_ORIGIN || 'https://nexus-engine.nexus-engine-website.workers.dev';
const get = async path => { const response = await fetch(new URL(path, origin), { cache: 'no-store' }); if (!response.ok) throw new Error(`HTTP ${response.status}: ${path}`); return response.json(); };
const catalog = await get('/data/catalog.json');
const launcherCatalog = await get('/releases.json');
// 公開直後の古いカタログ同士が一致しても、新版の検証成功とはしない。
// 今回生成した配布一覧そのものを基準にして、全版とsetupの反映を確認する。
const expected = JSON.parse(await readFile(new URL('../dist/data/catalog.json', import.meta.url), 'utf8'));
const expectedFiles = [...expected.releases, expected.launcher];
const publicFiles = [...catalog.releases, catalog.launcher];
if (expectedFiles.length !== publicFiles.length || expectedFiles.some(item => {
  return !publicFiles.some(published => {
    return published?.key === item.key && published.available && published.url === item.url && published.sha256 === item.sha256 && published.bytes === item.bytes;
  });
})) {
  throw new Error('Public catalog does not match this deployment. Wait for propagation and verify again.');
}
if (!catalog.launcher?.available || !catalog.releases.length || launcherCatalog.schemaVersion !== 1 || launcherCatalog.releases.length !== catalog.releases.length || catalog.releases.some(item => !launcherCatalog.releases.some(r => r.id === item.id && r.version === item.version && r.sha256 === item.sha256 && r.url === item.url))) throw new Error('Public catalog is incomplete.');
for (const item of [...catalog.releases, catalog.launcher]) {
  if (!item.available || !item.url.startsWith('https://github.com/kase-haruto/NexusEngine/releases/download/')) throw new Error('Invalid download address.');
  const response = await fetch(item.url);
  if (!response.ok) throw new Error(`Download failed: HTTP ${response.status}`);
  const hash = createHash('sha256'); let bytes = 0;
  for await (const chunk of response.body) { hash.update(chunk); bytes += chunk.length; }
  if (bytes !== item.bytes || hash.digest('hex') !== item.sha256) throw new Error('Public download hash or size mismatch.');
  console.log(`Public download verified: ${item.version || 'Launcher setup'} (${bytes} bytes)`);
}
console.log(`Verified site and all ${catalog.releases.length + 1} public downloads: ${origin}`);
