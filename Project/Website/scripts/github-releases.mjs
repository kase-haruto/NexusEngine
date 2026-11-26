import { execFileSync } from 'node:child_process';
import { readFile, writeFile, stat } from 'node:fs/promises';
import { createReadStream } from 'node:fs';
import { createHash } from 'node:crypto';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const repo = 'kase-haruto/NexusEngine';
const api = `https://api.github.com/repos/${repo}`;
const credential = execFileSync('git', ['credential', 'fill'], { cwd: resolve(root, '../..'), input: `protocol=https\nhost=github.com\npath=${repo}.git\n\n`, encoding: 'utf8', env: { ...process.env, GIT_TERMINAL_PROMPT: '0', GCM_INTERACTIVE: 'Never' }, stdio: ['pipe', 'pipe', 'pipe'] });
const token = credential.split(/\r?\n/).find(line => line.startsWith('password='))?.slice(9);
if (!token) throw new Error('GitHub authentication is required. Sign in through Git Credential Manager.');
const headers = { Authorization: `Bearer ${token}`, 'User-Agent': 'NexusEngine-Website', Accept: 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28' };
async function call(url, init = {}) {
  const response = await fetch(url, { ...init, headers: { ...headers, ...init.headers } });
  if (!response.ok) {
    const detail = await response.json().catch(() => ({}));
    throw new Error(`GitHub API returned ${response.status} for ${new URL(url).pathname}: ${JSON.stringify({ message: detail.message, errors: detail.errors })}`);
  }
  return response.status === 204 ? null : response.json();
}
const repository = await call(api);
if (repository.private || !repository.permissions?.push) throw new Error('A public repository with write access is required.');
if (process.argv.includes('--check')) console.log(`Authenticated write access confirmed: ${repo} (public).`);
else {
const manifest = JSON.parse(await readFile(resolve(root, 'upload-manifest.json'), 'utf8'));
const engineOnly = process.argv.includes('--engine-only');
const uploads = engineOnly ? manifest.filter(item => item.key.startsWith('engines/')) : manifest;
if (!uploads.some(item => item.key.startsWith('engines/')) ||
    (!engineOnly && uploads.filter(item => item.key.startsWith('launcher/')).length !== 1)) {
  throw new Error(engineOnly ? 'Build the SDK first.' : 'Build the SDKs and the production installer first.');
}
for (const item of uploads) {
  const hash = createHash('sha256'); for await (const chunk of createReadStream(item.file)) hash.update(chunk);
  if (hash.digest('hex') !== item.sha256 || (await stat(item.file)).size !== item.bytes || item.bytes >= 2 * 1024 ** 3) throw new Error('Artifact failed validation.');
}
const batch = createHash('sha256').update(uploads.map(item => item.sha256).join('\n')).digest('hex').slice(0, 12);
const tag = `distribution-${batch}`;
const existing = await call(api + '/releases?per_page=100');
let release = existing.find(item => item.tag_name === tag);
// 添付する実カタログから版一覧を生成し、次回の公開時も説明と配布物を一致させる。
const versions = uploads
  .filter(item => {
    return item.key.startsWith('engines/');
  })
  .map(item => {
    return item.key.replace(/^engines\/nexus-/, '').replace(/-Release\.zip$/, '');
  })
  .join(' / ');
const launcher = uploads.find(item => item.key.startsWith('launcher/'));
const contents = launcher ? `SDK ${versions}とLauncher ${launcher.version || ''} setup` : `SDK ${versions}`;
const notes = engineOnly && uploads.length === 1 && /^\d+\.\d+\.\d+$/.test(versions)
  ? (await readFile(resolve(root, '../Docs', `Release-${versions}.md`), 'utf8')) + '\n\n'
  : '';
const body = `NexusEngine Windows x64配布物。添付ファイルは${contents}です。\n\n` + notes + (launcher?.changes ? launcher.changes + '\n\n' : '') + '導入方法・変更点: https://nexus-engine.nexus-engine-website.workers.dev/\n\nこのタグは配布バッチを識別します。GitHubが自動生成するSource codeアーカイブは、タグが指すコミットのソースです。実行用ファイルはSDK ZIPを使用してください。\n\nSHA-256:\n' + uploads.map(item => `- ${item.key.split('/').at(-1)}: ${item.sha256}`).join('\n');
if (!release) {
  const commit = execFileSync('git', ['rev-parse', 'HEAD'], { cwd: resolve(root, '../..'), encoding: 'utf8' }).trim();
  // ローカルだけの開発commitを公開するために、別作業のソースをpushしない。
  // このタグは成果物の配布バッチ。ビルド入力は別の検証記録で特定する。
  const commitResponse = await fetch(api + `/commits/${commit}`, { headers });
  let targetCommit = commit;
  if (commitResponse.status === 404 || commitResponse.status === 422) {
    const remote = await call(api + `/commits/${encodeURIComponent(repository.default_branch)}`);
    targetCommit = remote.sha;
    console.log(`Local build commit ${commit} is not published; distribution tag uses remote commit ${targetCommit}. Build provenance remains in the verification notes.`);
  } else if (!commitResponse.ok) {
    throw new Error(`Cannot verify release target commit: HTTP ${commitResponse.status}`);
  }
  const name = engineOnly ? `NexusEngine ${versions} Windows x64` : 'NexusEngine Windows SDK / Launcher';
  release = await call(api + '/releases', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ tag_name: tag, target_commitish: targetCommit, name, body, draft: true }) });
  console.log(`Created draft release: ${tag}`);
}
// 前のsetupも版一覧から導入できるよう、検証済みの公開履歴を残す。
const published = JSON.parse(await readFile(resolve(root, 'published-assets.json'), 'utf8'));
for (const item of uploads) {
  const name = item.key.split('/').at(-1);
  let asset = release.assets.find(a => a.name === name);
  if (asset && (asset.size !== item.bytes || asset.digest !== `sha256:${item.sha256}`)) throw new Error(`Existing asset differs; refusing replacement: ${name}`);
  if (!asset) {
    console.log(`Uploading ${name} (${(item.bytes / 1048576).toFixed(1)} MB)...`);
    asset = await call(`https://uploads.github.com/repos/${repo}/releases/${release.id}/assets?name=${encodeURIComponent(name)}`, { method: 'POST', headers: { 'Content-Type': 'application/octet-stream', 'Content-Length': String(item.bytes) }, body: createReadStream(item.file), duplex: 'half' });
  }
  if (asset.size !== item.bytes || asset.digest !== `sha256:${item.sha256}` || asset.state !== 'uploaded') throw new Error(`Uploaded asset verification failed: ${name}`);
  published[item.key] = { url: asset.browser_download_url, bytes: asset.size, sha256: item.sha256, ...(item.version ? { version: item.version } : {}), ...(item.changes ? { changes: item.changes } : {}) };
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
