import { execFileSync } from 'node:child_process';
import { readFile, writeFile, stat } from 'node:fs/promises';
import { createReadStream } from 'node:fs';
import { createHash } from 'node:crypto';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const repo = 'kase-haruto/NexusEngine';
const api = `https://api.github.com/repos/${repo}`;
const credential = execFileSync('git', ['credential', 'fill'], { cwd: resolve(root, '../..'), input: `protocol=https\nhost=github.com\nusername=kase-haruto\npath=${repo}.git\n\n`, encoding: 'utf8', env: { ...process.env, GIT_TERMINAL_PROMPT: '0', GCM_INTERACTIVE: 'Never' }, stdio: ['pipe', 'pipe', 'pipe'] });
const token = credential.split(/\r?\n/).find(line => line.startsWith('password='))?.slice(9);
if (!token) throw new Error('GitHub authentication is required. Sign in through Git Credential Manager.');
const headers = { Authorization: `Bearer ${token}`, 'User-Agent': 'NexusEngine-Website', Accept: 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28' };
async function call(url, init = {}) {
  const response = await fetch(url, { ...init, headers: { ...headers, ...init.headers } });
  if (!response.ok) throw new Error(`GitHub API returned ${response.status} for ${new URL(url).pathname}`);
  return response.status === 204 ? null : response.json();
}
const repository = await call(api);
if (repository.private || !repository.permissions?.push) throw new Error('A public repository with write access is required.');
if (process.argv.includes('--check')) console.log(`Authenticated write access confirmed: ${repo} (public).`);
else {
const uploads = JSON.parse(await readFile(resolve(root, 'upload-manifest.json'), 'utf8'));
if (uploads.length < 2 || !uploads.some(item => item.key.startsWith('engines/')) || uploads.filter(item => item.key.startsWith('launcher/')).length !== 1) throw new Error('Build the SDKs and the production installer first.');
for (const item of uploads) {
  const hash = createHash('sha256'); for await (const chunk of createReadStream(item.file)) hash.update(chunk);
  if (hash.digest('hex') !== item.sha256 || (await stat(item.file)).size !== item.bytes || item.bytes >= 2 * 1024 ** 3) throw new Error('Artifact failed validation.');
}
const batch = createHash('sha256').update(uploads.map(item => item.sha256).join('\n')).digest('hex').slice(0, 12);
const tag = `distribution-${batch}`;
const existing = await call(api + '/releases?per_page=100');
let release = existing.find(item => item.tag_name === tag);
const versions = uploads.filter(item => item.key.startsWith('engines/')).map(item => item.key.match(/^engines\/nexus-(.+)-(?:Debug|Develop|Release)\.zip$/)?.[1]);
if (versions.some(version => !version)) throw new Error('Cannot determine SDK versions from upload manifest.');
const body = `NexusEngine Windows x64配布物。SDK ${versions.join(' / ')}とLauncher setupを添付しています。\n\n導入方法・変更点: https://nexus-engine.nexus-engine-website.workers.dev/\n\nこのタグは配布バッチを識別します。GitHubが自動生成するSource codeアーカイブは、このバッチの登録時点のソースであり、各旧SDKを生成した履歴の保証ではありません。実行用ファイルは添付のSDK ZIPとlauncherSetup.exeを使用してください。\n\nSHA-256:\n` + uploads.map(item => `- ${item.key.split('/').at(-1)}: ${item.sha256}`).join('\n');
if (!release) {
  const commit = execFileSync('git', ['rev-parse', 'HEAD'], { cwd: resolve(root, '../..'), encoding: 'utf8' }).trim();
  release = await call(api + '/releases', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ tag_name: tag, target_commitish: commit, name: 'NexusEngine Windows SDK / Launcher', body, draft: true }) });
  console.log(`Created draft release: ${tag}`);
}
const published = {};
for (const item of uploads) {
  const name = item.key.split('/').at(-1);
  let asset = release.assets.find(a => a.name === name);
  if (asset && (asset.size !== item.bytes || asset.digest !== `sha256:${item.sha256}`)) throw new Error(`Existing asset differs; refusing replacement: ${name}`);
  if (!asset) {
    console.log(`Uploading ${name} (${(item.bytes / 1048576).toFixed(1)} MB)...`);
    asset = await call(`https://uploads.github.com/repos/${repo}/releases/${release.id}/assets?name=${encodeURIComponent(name)}`, { method: 'POST', headers: { 'Content-Type': 'application/octet-stream', 'Content-Length': String(item.bytes) }, body: createReadStream(item.file), duplex: 'half' });
  }
  if (asset.size !== item.bytes || asset.digest !== `sha256:${item.sha256}` || asset.state !== 'uploaded') throw new Error(`Uploaded asset verification failed: ${name}`);
  published[item.key] = { url: asset.browser_download_url, bytes: asset.size, sha256: item.sha256 };
  console.log(`Verified ${name}`);
}
if (release.draft) release = await call(api + `/releases/${release.id}`, { method: 'PATCH', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ draft: false, make_latest: 'true' }) });
// Draft asset URLs use a temporary untagged path; refresh after publication.
release = await call(api + `/releases/${release.id}`);
for (const item of uploads) {
  const asset = release.assets.find(a => a.name === item.key.split('/').at(-1));
  if (!asset || asset.size !== item.bytes || asset.digest !== `sha256:${item.sha256}`) throw new Error('Published asset verification failed.');
  published[item.key].url = asset.browser_download_url;
}
await writeFile(resolve(root, 'published-assets.json'), JSON.stringify(published, null, 2));
console.log(`Published verified release: ${release.html_url}`);
}
