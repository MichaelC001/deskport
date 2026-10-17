#!/usr/bin/env node
// Real Chromium, production HTML/JS/CSS, loopback HTTPS and synthetic API data only.
// No capture, clipboard access, OS input injection or connection to a DeskPort host.
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import https from 'node:https';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.DESKPORT_PLAYWRIGHT_MODULE || 'playwright-core');
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
assert.ok(process.env.DESKPORT_FRONTEND_TEST_OUTPUT, 'Set an output directory under the resolved build root.');
const output = process.env.DESKPORT_FRONTEND_TEST_OUTPUT;
await mkdir(output, { recursive: true });
const key = path.join(output, 'fixture-key.pem'), cert = path.join(output, 'fixture-cert.pem');
execFileSync('openssl', ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', key, '-out', cert, '-days', '1', '-subj', '/CN=localhost'], { stdio: 'ignore' });
const resources = new Map(await Promise.all(['index.html', 'app.js', 'i18n.js', 'i18n-data.js', 'style.css'].map(async name => [name, await readFile(path.join(root, 'app/browser', name))])));
resources.set('icon.svg', await readFile(path.join(root, 'app/res/deskport.svg')));
resources.set('index.html', Buffer.from(resources.get('index.html').toString().replace('@DESKPORT_VERSION@', '0.7.2-fixture')));
const server = https.createServer({ key: await readFile(key), cert: await readFile(cert) }, (request, response) => {
  const name = request.url === '/' ? 'index.html' : request.url.slice(1);
  if (!resources.has(name)) { response.writeHead(404); response.end(); return; }
  response.setHeader('Content-Type', name.endsWith('.js') ? 'application/javascript; charset=utf-8' : name.endsWith('.css') ? 'text/css' : name.endsWith('.svg') ? 'image/svg+xml' : 'text/html; charset=utf-8');
  response.setHeader('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; media-src 'self' blob:; img-src 'self' data:; object-src 'none'; frame-ancestors 'none'");
  response.end(resources.get(name));
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `https://127.0.0.1:${server.address().port}`;
const browser = await chromium.launch({ headless: true, args: ['--disable-gpu'], ...(process.env.DESKPORT_TEST_CHROME ? { executablePath: process.env.DESKPORT_TEST_CHROME } : {}) });
const checks = [], errors = [], screenshots = [], fonts = [], contexts = [], requests = [], pendingRoutes = [];
const check = (name, condition = true) => { assert.ok(condition, name); checks.push(name); };
let page;
async function fixture(locale, options = {}) {
  const context = await browser.newContext({ ignoreHTTPSErrors: true, locale, viewport: { width: 1180, height: 820 } });
  contexts.push(context);
  if (options.preference !== undefined) await context.addInitScript(value => localStorage.setItem('deskport.language', value), options.preference);
  if (options.blockStorage) await context.addInitScript(() => {
    for (const method of ['getItem', 'setItem', 'removeItem']) Storage.prototype[method] = () => { throw new DOMException('Fixture storage blocked', 'SecurityError'); };
  });
  const state = { mode: options.mode || 'unpaired', errorCode: 'unauthorized', rawMessage: '' };
  await context.route('**/api/**', async route => {
    const endpoint = new URL(route.request().url()).pathname;
    requests.push({ endpoint, body: route.request().postDataJSON() });
    let status = 200, body = { ok: true };
    if (endpoint === '/api/resume') {
      if (state.mode === 'paired') body = { ok: true, csrfToken: 'synthetic-csrf', paired: true, pairing: { id: 'synthetic-pair', name: 'QA <b>name</b> العربية 日本語' } };
      else { status = 401; body = { ok: false, code: 'unpaired' }; }
    } else if (endpoint === '/api/login/tailnet') { status = 403; body = { ok: false, code: 'tailnet-unavailable' }; }
    else if (endpoint === '/api/login') {
      if (state.mode === 'connecting') body = { ok: true, csrfToken: 'synthetic-csrf', paired: false };
      else { status = 403; body = { ok: false, code: state.errorCode, ...(state.rawMessage ? { message: state.rawMessage } : {}) }; }
    } else if (endpoint === '/api/status') body = { ok: true, hostName: 'QA <script>synthetic</script> العربية 日本語', sharing: true, mediaAvailable: true, capabilities: { resize: false, extendDisplay: true, zoomChoices: [0.5, 1, 1.5] } };
    else if (endpoint === '/api/session/start') { await new Promise(resolve => pendingRoutes.push(resolve)); status = 503; body = { ok: false, code: 'media-unavailable' }; }
    await route.fulfill({ status, contentType: 'application/json', body: JSON.stringify(body) });
  });
  const page = await context.newPage();
  page.on('pageerror', error => errors.push(error.message));
  await page.goto(origin);
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle');
  await page.evaluate(() => document.fonts.ready);
  return { page, context, state };
}
async function screenshot(page, name) {
  await page.evaluate(() => document.fonts.ready);
  await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  await page.screenshot({ path: path.join(output, name+'.png'), fullPage: true, animations: 'disabled' });
  screenshots.push(name+'.png');
}
async function noOverflow(page, label) {
  check(label, await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
}
async function fontEvidence(page, locale) {
  const cdp = await page.context().newCDPSession(page);
  await cdp.send('DOM.enable'); await cdp.send('CSS.enable');
  const { root } = await cdp.send('DOM.getDocument');
  const { nodeId } = await cdp.send('DOM.querySelector', { nodeId: root.nodeId, selector: '#login-title [data-i18n]' });
  const result = await cdp.send('CSS.getPlatformFontsForNode', { nodeId });
  fonts.push({ locale, fonts: result.fonts });
  await cdp.detach();
}
try {
  ({ page } = await fixture('ar-EG'));
  check('initial regional Arabic selects ar and RTL', await page.evaluate(() => document.documentElement.lang === 'ar' && document.documentElement.dir === 'rtl'));
  check('22 registered languages plus system default', await page.locator('#language option').count() === 23);
  const beforeInvalid = requests.length;
  await page.locator('#access-code').fill('12');
  await page.locator('#connect').click();
  check('native form validation uses the selected UI language', await page.locator('#notice').textContent() === await page.evaluate(() => DeskPortI18n.t('Enter the 6-digit code from the computer or your authenticator app.')));
  check('invalid code never starts authentication', requests.length === beforeInvalid);
  await page.locator('#access-code').fill('');
  const initialRequests = requests.length;
  for (const locale of await page.evaluate(() => DeskPortUIResources.languages.map(l => l.code))) {
    await page.selectOption('#language', locale);
    check(`${locale}: document, title and placeholder follow selection`, await page.evaluate(code =>
      document.documentElement.lang === code && document.title === DeskPortI18n.t('DeskPort · Browser connection') &&
      document.getElementById('access-code').placeholder === DeskPortI18n.t('6 digits'), locale));
  }
  check('all language switches leave authentication untouched', requests.length === initialRequests);
  check('placeholder substitution preserves data and isolates bidi', await page.evaluate(() =>
    DeskPortI18n.t('Remote input disconnected. %1', '<img> %2') .includes('\u2068<img> %2\u2069')));
  check('unknown source and missing cell use English', await page.evaluate(() => {
    delete DeskPortUIResources.strings.ms['Browser connection'];
    return DeskPortI18n.t('Browser connection') === 'Browser connection' && DeskPortI18n.t('Unknown fixture key') === 'Unknown fixture key' && DeskPortI18n.t('constructor') === 'constructor' && DeskPortI18n.t('__proto__') === '__proto__';
  }));
  for (const [input, expected] of [['zh-TW','zh-Hant'], ['zh-HK','zh-Hant'], ['zh-Hans-TW','zh-Hans'], ['zh-SG','zh-Hans'], ['pt-BR','pt'], ['in-ID','id'], ['bo-CN','bo'], ['unknown','en']]) {
    check(`${input}: locale matching`, await page.evaluate(([input, expected]) => DeskPortI18n.resolve(input, []) === expected, [input, expected]));
  }
  check('ordered browser fallback skips unsupported languages', await page.evaluate(() => DeskPortI18n.resolve('', ['xx', 'ja-JP', 'fr']) === 'ja'));
  await page.selectOption('#language', 'ar');
  await page.locator('#access-code').fill('123456');
  await page.locator('#connect').click();
  await page.waitForFunction(() => !document.getElementById('notice').hidden);
  check('login error cannot cover the connect action', await page.evaluate(() => document.getElementById('notice').getBoundingClientRect().bottom <= document.getElementById('connect').getBoundingClientRect().top));
  const arNotice = await page.locator('#notice').textContent();
  await page.selectOption('#language', 'de');
  check('existing error changes language without a new request', arNotice !== await page.locator('#notice').textContent() && await page.locator('#notice').textContent() === await page.evaluate(() => DeskPortI18n.t('Incorrect or expired code. Enter the current 6-digit code.')));
  for (const locale of ['ar', 'bo', 'zh-Hant', 'ja', 'ko', 'de', 'hi', 'th']) {
    await page.selectOption('#language', locale);
    await page.setViewportSize({ width: 390, height: 844 });
    await noOverflow(page, `${locale}: narrow login has no horizontal overflow`);
    // Keep fixture errors visible while recording representative font/layout output.
    await screenshot(page, `login-${locale}-390`);
    await fontEvidence(page, locale);
  }
  await page.selectOption('#language', 'de');
  await page.reload();
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle');
  check('saved locale survives reload', await page.evaluate(() => document.documentElement.lang === 'de' && localStorage.getItem('deskport.language') === 'de'));
  const otherTab = await page.context().newPage();
  await otherTab.goto(origin);
  await otherTab.waitForFunction(() => document.documentElement.lang === 'de');
  await page.selectOption('#language', 'ar');
  await otherTab.waitForFunction(() => document.documentElement.lang === 'ar');
  check('same-origin tabs follow preference storage events');
  await page.selectOption('#language', '');
  check('system default clears preference and restores navigator locale', await page.evaluate(() => !localStorage.getItem('deskport.language') && document.documentElement.lang === 'ar'));
  await otherTab.close();
  const blocked = await fixture('bo-CN', { blockStorage: true });
  check('blocked storage uses navigator Tibetan', await blocked.page.evaluate(() => document.documentElement.lang === 'bo'));
  await blocked.page.selectOption('#language', 'ar');
  check('blocked storage still supports in-visit switching', await blocked.page.evaluate(() => document.documentElement.lang === 'ar'));
  const unsupported = await fixture('xx-XX', { preference: 'xx-XX' });
  check('unsupported preference and navigator fall back to English', await unsupported.page.evaluate(() => document.documentElement.lang === 'en'));
  const aborted = await fixture('ar');
  await aborted.page.evaluate(() => {
    const originalFetch = window.fetch;
    window.fetch = (url, ...args) => String(url) === '/api/login'
      ? Promise.reject(new DOMException('Synthetic aborted fetch', 'AbortError')) : originalFetch(url, ...args);
  });
  await aborted.page.locator('#access-code').fill('123456');
  await aborted.page.locator('#connect').click();
  check('native AbortError numeric code uses the shared timeout message', await aborted.page.locator('#notice').textContent() === await aborted.page.evaluate(() => DeskPortI18n.t('Connection timed out. Check the computer and network.')));
  const paired = await fixture('ar', { mode: 'paired' });
  check('paired name is literal text', await paired.page.locator('#paired-name').textContent() === 'QA <b>name</b> العربية 日本語' && await paired.page.locator('#paired-name b').count() === 0);
  await paired.page.selectOption('#language', 'ja');
  check('locale changes preserve paired state and user name', await paired.page.locator('#paired-ready').isVisible() && await paired.page.locator('#paired-name').textContent() === 'QA <b>name</b> العربية 日本語');
  const connecting = await fixture('ar', { mode: 'connecting' });
  page = connecting.page;
  await page.locator('#access-code').fill('123456');
  await page.locator('#connect').click();
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'connecting');
  const sessionRequests = requests.length;
  await page.selectOption('#language', 'bo');
  check('language change preserves connecting session', await page.locator('#connection-status').getAttribute('data-state') === 'connecting' && requests.length === sessionRequests);
  check('host name is isolated literal text', await page.locator('#host-name').textContent() === 'QA <script>synthetic</script> العربية 日本語' && await page.locator('#host-name script').count() === 0);
  for (const locale of ['ar','bo','zh-Hant','ja','de']) {
    await page.selectOption('#language', locale);
    await page.setViewportSize({ width: 320, height: 568 });
    await noOverflow(page, `${locale}: narrow session has no horizontal overflow`);
    await screenshot(page, `session-${locale}-320`);
  }
  // Pseudolocalization is fixture-only; it is never exported into the shipped table.
  await page.evaluate(() => {
    for (const [key, text] of Object.entries(DeskPortUIResources.strings.de)) DeskPortUIResources.strings.de[key] = `[${text} ${text} ${text}]`;
    DeskPortI18n.select('de');
  });
  await noOverflow(page, 'triple-length session has no horizontal overflow');
  check('long connecting copy keeps its spinner and caption inside the placeholder', await page.locator('#video-placeholder').evaluate(element => {
    const spinner = element.querySelector('.spinner').getBoundingClientRect();
    const caption = element.lastElementChild.getBoundingClientRect();
    return spinner.height === 28 && caption.bottom <= element.getBoundingClientRect().bottom;
  }));
  await screenshot(page, 'session-long-320');
  await page.goto(origin);
  await page.waitForFunction(() => document.getElementById('connection-status').dataset.state === 'idle');
  await page.evaluate(() => {
    for (const [key, text] of Object.entries(DeskPortUIResources.strings.de)) DeskPortUIResources.strings.de[key] = `[${text} ${text} ${text}]`;
    DeskPortI18n.select('de');
  });
  await page.locator('details').evaluate(element => { element.open = true; });
  await noOverflow(page, 'triple-length login/options have no horizontal overflow');
  await screenshot(page, 'login-long-320');
  const raw = await fixture('ar');
  raw.state.errorCode = 'constructor'; raw.state.rawMessage = '<b>RAW %1 العربية 日本語</b>';
  await raw.page.locator('#access-code').fill('123456'); await raw.page.locator('#connect').click();
  await raw.page.waitForFunction(() => !document.getElementById('notice').hidden);
  await raw.page.selectOption('#language', 'bo');
  check('unknown server error is literal and never a template or HTML', await raw.page.locator('#notice').textContent() === raw.state.rawMessage && await raw.page.locator('#notice b').count() === 0);
  const scriptless = await browser.newContext({ ignoreHTTPSErrors: true, javaScriptEnabled: false });
  contexts.push(scriptless);
  const staticPage = await scriptless.newPage();
  const nativeSubmissions = [];
  staticPage.on('request', request => {
    if (request.method() === 'POST' || request.url().includes('access-code=')) nativeSubmissions.push(request.url());
  });
  await staticPage.goto(origin);
  check('connection stays disabled before script initialization', await staticPage.locator('#connect').isDisabled());
  await staticPage.locator('#access-code').fill('123456');
  await staticPage.locator('#access-code').press('Enter');
  await staticPage.waitForTimeout(100);
  check('script failure cannot submit a code or place it in the URL', nativeSubmissions.length === 0 && !staticPage.url().includes('123456'));
  check('no uncaught browser errors', errors.length === 0);
  await writeFile(path.join(output, 'results.json'), JSON.stringify({ browser: browser.version(), checks, count: checks.length, screenshots, fonts, errors, scope: 'Production Web UI with synthetic loopback HTTPS/API, no native capture, OS input, personal host or deployment. Font inventory is evidence, not glyph/native-speaker acceptance.' }, null, 2));
  console.log(JSON.stringify({ count: checks.length, screenshots, fonts, errors }, null, 2));
} catch (error) {
  if (page && !page.isClosed()) await screenshot(page, 'failure');
  throw error;
} finally {
  pendingRoutes.forEach(resolve => resolve());
  for (const context of contexts) await context.close();
  await browser.close();
  await new Promise(resolve => server.close(resolve));
}
