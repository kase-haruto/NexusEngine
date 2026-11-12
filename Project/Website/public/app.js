const $ = id => document.getElementById(id);
const size = bytes => `${(bytes / 1048576).toFixed(1)} MB`;
let releases = [];
function element(tag, className, text) { const el = document.createElement(tag); if (className) el.className = className; if (text !== undefined) el.textContent = text; return el; }
function downloadLink(item, label) {
  const link = element('a', 'button secondary', item.available ? label : '公開準備中');
  if (item.available) {
    const url = new URL(item.url, location.origin);
    if (url.protocol === 'https:' && url.hostname === 'github.com' && !url.username && !url.password && !url.port && /^\/kase-haruto\/NexusEngine\/releases\/download\/distribution-[a-f0-9]{12}\/[a-zA-Z0-9._-]+$/.test(url.pathname)) { link.href = url.href; link.setAttribute('download', ''); }
    else { link.textContent = '配布URLを確認中'; link.setAttribute('aria-disabled', 'true'); }
  } else link.setAttribute('aria-disabled', 'true');
  return link;
}
async function copy(text, button) {
  try { await navigator.clipboard.writeText(text); button.textContent = 'コピーしました'; }
  catch { button.textContent = 'コピーできません。文字列を選択してください'; }
  setTimeout(() => button.textContent = 'コピー', 2500);
}
function render() {
  const query = $('release-search').value.trim().toLowerCase();
  const filtered = releases.filter(r => `${r.version} ${r.configuration} ${r.changes}`.toLowerCase().includes(query));
  $('releases').replaceChildren();
  $('release-status').textContent = filtered.length ? `${filtered.length} バージョン / Windows x64` : '一致するバージョンがありません。';
  filtered.forEach(r => {
    const article = element('article', 'release');
    const top = element('div', 'release-top'); const identity = element('div'); const version = element('div', 'version-number', `v${r.version}`);
    if (r === releases[0]) version.append(element('span', 'badge', 'LATEST'));
    identity.append(version, element('div', 'release-meta', `${r.configuration} / ${size(r.bytes)}`));
    const intro = r.changes.split(/\r?\n/).find(line => line.trim() && !line.startsWith('#')) || '変更内容は詳細をご確認ください。';
    top.append(identity, element('p', 'release-summary', intro), downloadLink(r, 'SDK ZIP ↓'));
    const details = element('details'); details.append(element('summary', '', '変更点・互換性・配布情報'));
    // Release notes are plain text: never execute catalog HTML or remote Markdown.
    details.append(element('div', 'changes', r.changes));
    const checksum = element('div', 'checksum'); checksum.append(element('span', '', 'SHA-256: '), element('code', '', r.sha256));
    const button = element('button', 'copy', 'コピー'); button.type = 'button'; button.addEventListener('click', () => copy(r.sha256, button)); checksum.append(button);
    details.append(checksum, element('p', 'checksum', r.available ? `配布元: ${r.url}` : 'このバージョンの配布ファイルはまだ公開されていません。'));
    article.append(top, details); $('releases').append(article);
  });
}
async function load() {
  $('retry').hidden = true; $('release-status').textContent = 'バージョン情報を読み込んでいます。';
  try {
    const response = await fetch('/data/catalog.json', { cache: 'no-store', signal: AbortSignal.timeout(15000) });
    if (!response.ok) throw new Error('catalog');
    const data = await response.json(); if (data.schemaVersion !== 1 || !Array.isArray(data.releases)) throw new Error('schema');
    releases = data.releases; render();
    if (releases.length) $('hero-latest').textContent = `NexusEngine ${releases[0].version} — 変更点を見る`;
    const setup = data.launcher;
    $('setup-size').textContent = setup ? size(setup.bytes) : '公開時に表示';
    if (setup?.available) {
      const link = downloadLink(setup, 'Windows版をダウンロード ↓'); link.id = 'setup-download'; link.className = 'button primary'; $('setup-download').replaceWith(link);
      $('setup-status').textContent = 'Windows x64 / GitHub Releasesからダウンロードします。';
      $('setup-hash').textContent = setup.sha256; $('setup-checksum').hidden = false;
      $('copy-setup').onclick = () => copy(setup.sha256, $('copy-setup'));
    } else {
      $('setup-download').removeAttribute('href'); $('setup-download').setAttribute('aria-disabled', 'true'); $('setup-download').textContent = 'セットアップ公開準備中';
      $('setup-status').textContent = 'セットアップの公開後、このページからダウンロードできます。';
      $('setup-checksum').hidden = true;
    }
  } catch {
    $('release-status').textContent = 'バージョン情報を取得できませんでした。接続を確認して再読み込みしてください。'; $('retry').hidden = false;
    $('setup-download').removeAttribute('href'); $('setup-download').setAttribute('aria-disabled', 'true'); $('setup-download').textContent = '配布状況を取得できません';
    $('setup-status').textContent = 'バージョン一覧の「再読み込み」から再試行できます。';
  }
}
$('release-search').addEventListener('input', render); $('retry').addEventListener('click', load); load();
