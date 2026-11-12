import test from 'node:test';
import assert from 'node:assert/strict';
import { within, renderDoc, publishedDownload } from '../scripts/build.mjs';
import { readFile } from 'node:fs/promises';
const item = { key: 'engines/nexus-1.1.1-Release.zip', bytes: 123, sha256: 'a'.repeat(64) };
const asset = { url: 'https://github.com/kase-haruto/NexusEngine/releases/download/distribution-abcdef012345/nexus-1.1.1-Release.zip', bytes: 123, sha256: item.sha256 };
test('only an uploaded GitHub asset matching the local checksum and size becomes downloadable', () => {
  assert.equal(publishedDownload(item, {}).available, false);
  assert.equal(publishedDownload(item, { [item.key]: asset }).available, true);
  for (const changed of [{ bytes: 122 }, { sha256: 'b'.repeat(64) }, { url: 'http://github.com/file.zip' }, { url: asset.url.replace('github.com', 'github.com.evil.example') }, { url: asset.url.replace('kase-haruto', 'other-owner') }]) {
    assert.throws(() => publishedDownload(item, { [item.key]: { ...asset, ...changed } }));
  }
});
test('packaging refuses traversal and absolute package paths', () => {
  assert.throws(() => within('C:/release', '../outside.zip'));
  assert.throws(() => within('C:/release', 'C:/outside.zip'));
  assert.match(within('C:/release', 'v1/package.zip'), /package.zip$/);
});
test('documentation removes executable HTML and rewrites internal markdown links', () => {
  const html = renderDoc('# Title\n\n[Guide](GamepadInputREADME.md)\n\n<script>alert(1)</script>\n\n[x](javascript:alert(1))');
  assert.match(html, /<h1>Title<\/h1>/); assert.match(html, /href="GamepadInputREADME.html"/);
  assert.doesNotMatch(html, /<script|javascript:/);
});
test('hosting deploys only static assets and has no usage-billed storage bindings', async () => {
  const config = JSON.parse(await readFile(new URL('../wrangler.jsonc', import.meta.url), 'utf8'));
  assert.equal(config.main, undefined);
  assert.equal(config.assets.directory, './dist');
  for (const binding of ['r2_buckets', 'kv_namespaces', 'd1_databases', 'durable_objects']) assert.equal(config[binding], undefined);
});
