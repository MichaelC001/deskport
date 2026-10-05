#!/usr/bin/env node
// Real HTTPS, production frontend, disposable persistent Chromium profiles.
// HTTP is never intercepted or mocked; host revocation/restart uses a private
// local fixture socket. Credentials and cookie values never enter result files.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFile, writeFile, mkdir} from 'node:fs/promises';
import net from 'node:net';
import path from 'node:path';

const index = process.argv.indexOf('--fixture');
assert.ok(index >= 0 && process.argv[index + 1], 'Pass --fixture FILE');
const fixture = JSON.parse(await readFile(process.argv[index + 1], 'utf8'));
assert.ok(fixture.controlSocket && new URL(fixture.url).protocol === 'https:');
const require = createRequire(import.meta.url);
const {chromium} = require(process.env.DESKPORT_PLAYWRIGHT_MODULE || 'playwright-core');
const output = fixture.outputDir;
await mkdir(output, {recursive:true});
const checks = [], media = [], api = [], cookieFlags = [], pageErrors = [];
let context, page, browserVersion, sequence = 0, lastCsrf = '';
const check = (name, condition = true) => { assert.ok(condition, name); checks.push(name); };
const delay = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
function control(action, extra = {}) {
  return new Promise((resolve,reject) => {
    const socket = net.createConnection(fixture.controlSocket);
    const timer = setTimeout(() => { socket.destroy(); reject(new Error('Fixture control timeout')); }, 30000);
    let data = '';
    socket.on('connect', () => socket.write(JSON.stringify({action,requestId:String(++sequence),...extra})+'\n'));
    socket.on('data', bytes => {
      data += bytes;
      if (!data.includes('\n')) return;
      clearTimeout(timer); socket.end();
      try { const value = JSON.parse(data.split('\n')[0]); assert.ok(value.ok, 'Fixture control '+action); resolve(value); }
      catch(error) { reject(error); }
    });
    socket.on('error', error => { clearTimeout(timer); reject(error); });
  });
}
async function launch(profile) {
  lastCsrf='';
  context = await chromium.launchPersistentContext(path.join(output,profile), {
    executablePath:fixture.chromium, headless:true, ignoreHTTPSErrors:true,
    viewport:{width:1280,height:890}, hasTouch:true
  });
  browserVersion = context.browser().version();
  await context.grantPermissions(['local-network-access'],{origin:new URL(fixture.url).origin}).catch(()=>{});
  await context.addInitScript(() => {
    const Original = window.RTCPeerConnection;
    window.pairingTestPeers = [];
    window.RTCPeerConnection = class extends Original {
      constructor(...args) { super(...args); window.pairingTestPeers.push(this); }
    };
  });
  page = context.pages()[0] || await context.newPage(); page.setDefaultTimeout(25000);
  page.on('pageerror',error => pageErrors.push(error.message));
  page.on('response',async response => {
    const pathname = new URL(response.url()).pathname;
    if (!pathname.startsWith('/api/')) return;
    api.push({path:pathname,status:response.status()});
    if(['/api/login','/api/resume'].includes(pathname) && response.ok()) {
      const authenticated=await response.json();
      if(authenticated.csrfToken) lastCsrf=authenticated.csrfToken;
    }
    if (pathname === '/api/login') {
      for (const entry of await response.headersArray()) if (entry.name.toLowerCase() === 'set-cookie') {
        const [pair,...flags] = entry.value.split(';');
        cookieFlags.push({name:pair.split('=')[0],flags:flags.map(value=>value.trim())});
      }
    }
  });
  await page.goto(fixture.url);
}
async function close() { if(context) await context.close(); context=null; page=null; }
async function ready(paired, name) {
  await page.waitForFunction(expected => document.getElementById('connection-status')?.dataset.state === 'idle' &&
    document.body.dataset.paired === String(expected), paired);
  check(name+': expected pairing state', await page.locator('#paired-ready').isVisible() === paired);
  check(name+': no automatic stream', await page.evaluate(()=>window.pairingTestPeers.length === 0 ||
    window.pairingTestPeers.every(peer=>peer.connectionState === 'closed')));
  check(name+': no browser storage secrets', await page.evaluate(()=>localStorage.length===0 && sessionStorage.length===0 &&
    !document.cookie.includes('__Host-deskport-pairing') && !document.cookie.includes('__Host-deskport-session')));
}
async function connectWithCode(remember, name) {
  await page.locator('#remember-browser').setChecked(remember);
  if (remember) await page.locator('#device-name').fill(name);
  await page.locator('#access-code').fill(fixture.code);
  await page.locator('#connect').click();
  if(fixture.authOnly) {
    // This separate branded-browser fixture has no capture host. Its real
    // gateway accepts login, then the expected unavailable-media path returns
    // to the production paired-ready UI without touching a personal desktop.
    await ready(remember,name+' auth-only login');
    return;
  }
  await page.waitForFunction(()=>document.getElementById('connection-status').dataset.state==='streaming',null,{timeout:55000});
  await page.waitForFunction(()=>document.getElementById('desktop-video').videoWidth===1280);
  check(name+': access code erased after login',await page.locator('#access-code').inputValue()==='');
}
async function sample(label) {
  if(fixture.authOnly) return;
  const read = () => page.evaluate(async()=>{
    const peer=window.pairingTestPeers.at(-1), reports=await peer.getStats();
    const video=[...reports.values()].find(row=>row.type==='inbound-rtp' && row.kind==='video');
    const audio=[...reports.values()].find(row=>row.type==='inbound-rtp' && row.kind==='audio');
    const element=document.getElementById('desktop-video');
    return {state:peer.connectionState,width:element.videoWidth,height:element.videoHeight,
      frames:video?.framesDecoded||0,audioPackets:audio?.packetsReceived||0,energy:audio?.totalAudioEnergy||0};
  });
  if(await page.locator('#resume-audio').isVisible()) await page.locator('#resume-audio').click();
  await delay(1000); const first=await read(); await delay(2000); const second=await read();
  check(label+': real native H264 continues',second.state==='connected' && second.width===1280 && second.height===720 && second.frames>first.frames+20);
  check(label+': real native audio continues',second.audioPackets>first.audioPackets && second.energy>first.energy);
  media.push({label,first,second});
  await page.screenshot({path:path.join(output,label+'-media.png')});
}
async function disconnect(paired,label) {
  if(fixture.authOnly) {
    assert.ok(lastCsrf,'Auth-only fixture retains the real response CSRF in test memory');
    const response=await context.request.post(fixture.url+'api/logout',{data:{},
      headers:{Origin:new URL(fixture.url).origin,'X-DeskPort-Session':lastCsrf}});
    check(label+': real logout endpoint succeeds',response.ok());
    lastCsrf=''; await page.reload(); await ready(paired,label);
    return;
  }
  await page.locator('#disconnect').click(); await ready(paired,label);
  check(label+': local media cleared',await page.evaluate(()=>document.getElementById('desktop-video').srcObject===null));
}
async function profileCookie() { return (await context.cookies(fixture.url)).find(cookie=>cookie.name==='__Host-deskport-pairing'); }
async function noOwner() {
  for(let i=0;i<80;i++) { if(!(await control('status')).hasOwner) return; await delay(250); }
  throw new Error('Revocation left native admission occupied');
}

let result;
try {
  await launch('remembered-profile'); await ready(false,'new profile');
  check('remember option defaults on',await page.locator('#remember-browser').isChecked());
  await connectWithCode(true,'Remembered fixture'); await sample('paired-initial');
  const idleTab=await context.newPage();
  idleTab.on('pageerror',error=>pageErrors.push(error.message));
  await idleTab.goto(fixture.url);
  await idleTab.waitForFunction(()=>document.body.dataset.paired==='true' && document.getElementById('connection-status').dataset.state==='idle');
  check('second paired tab resumes without starting media',await idleTab.evaluate(()=>window.pairingTestPeers.length===0));
  await idleTab.close();
  if(!fixture.authOnly) await sample('after-idle-tab-closed');
  check(fixture.authOnly?'closing idle paired tab preserves pairing':'closing an idle paired tab preserves the owning tab',
    await page.evaluate(authOnly=>authOnly?document.body.dataset.paired==='true':window.pairingTestPeers.at(-1).connectionState==='connected',Boolean(fixture.authOnly)));
  const cookie=await profileCookie();
  check('persistent cookie is Secure HttpOnly Strict host cookie',cookie?.secure && cookie.httpOnly && cookie.sameSite==='Strict' && cookie.path==='/' && cookie.domain==='127.0.0.1');
  check('pairing expires in one year',cookie.expires-Date.now()/1000>31535000 && cookie.expires-Date.now()/1000<31537000);
  check('short session cookie has no persistent expiry',(await context.cookies()).filter(c=>c.name!==cookie.name && c.name.startsWith('__Host-')).every(c=>c.expires===-1));
  check('pairing Set-Cookie has no Domain and declares one year',cookieFlags.some(c=>c.name===cookie.name && c.flags.includes('Max-Age=31536000') && !c.flags.some(flag=>flag.toLowerCase().startsWith('domain='))));
  await disconnect(true,'normal disconnect preserves pairing');
  if(!fixture.authOnly) {
    const resumed=await context.request.post(fixture.url+'api/resume',{data:{},headers:{Origin:new URL(fixture.url).origin}});
    const authentication=await resumed.json();
    const loggedOut=await context.request.post(fixture.url+'api/logout',{data:{},
      headers:{Origin:new URL(fixture.url).origin,'X-DeskPort-Session':authentication.csrfToken}});
    check('explicit logout succeeds without forgetting pairing',loggedOut.ok());
  }
  check('logout keeps pairing cookie',Boolean(await profileCookie()));
  const startsBeforeRefresh=api.filter(row=>row.path==='/api/session/start').length;
  await page.reload(); await ready(true,'refresh resumes pairing');
  check('refresh never starts media',api.filter(row=>row.path==='/api/session/start').length===startsBeforeRefresh);
  await close(); await launch('remembered-profile'); await ready(true,'full browser restart resumes pairing');
  const firstId=(await control('list')).pairedBrowsers[0]?.id;
  check('computer lists a remembered browser',Boolean(firstId));
  await control('restart'); await page.reload(); await ready(true,'same-port server restart resumes pairing');
  check('server restart retains pairing identity',(await control('list')).pairedBrowsers.some(row=>row.id===firstId));
  await page.screenshot({path:path.join(output,'paired-ready-after-restarts.png')});
  await close();

  await launch('separate-profile'); await ready(false,'different profile cannot inherit pairing');
  await connectWithCode(false,'Temporary fixture'); await disconnect(false,'temporary disconnect');
  check('temporary mode creates no pairing cookie',!await profileCookie());
  await close(); await launch('separate-profile'); await ready(false,'temporary browser restart requires code'); await close();

  await launch('remembered-profile'); await ready(true,'original profile remains paired');
  await page.locator('#forget-pairing').click(); await ready(false,'forget removes browser trust');
  check('forget clears persistent cookie',!await profileCookie());
  check('forget removes computer pairing record',!(await control('list')).pairedBrowsers.some(row=>row.id===firstId));
  await connectWithCode(true,'Revocation fixture'); await sample('before-revocation');
  const revokedCookie=await profileCookie(), revokedId=(await control('list')).pairedBrowsers[0].id;
  await control('revoke',{id:revokedId});
  if(fixture.authOnly) await page.reload();
  await ready(false,'computer revocation ends active browser'); await noOwner();
  check(fixture.authOnly?'revocation leaves no active owner':'revocation releases native capture owner',true);
  check('revocation clears media element',await page.evaluate(()=>document.getElementById('desktop-video').srcObject===null));
  const stale = await chromium.launchPersistentContext(path.join(output,'stale-cookie-profile'),{
    executablePath:fixture.chromium,headless:true,ignoreHTTPSErrors:true
  });
  try {
    await stale.addCookies([revokedCookie]);
    const response=await stale.request.post(fixture.url+'api/resume',{data:{},headers:{Origin:new URL(fixture.url).origin}});
    check('revoked old cookie cannot resume',response.status()===401 && (await response.json()).code==='unpaired');
    const retried=await stale.request.post(fixture.url+'api/resume',{data:{},headers:{Origin:new URL(fixture.url).origin}});
    check('replaying revoked cookie still cannot authorize',retried.status()===401 && (await retried.json()).code==='unpaired');
  } finally {await stale.close();}
  await connectWithCode(true,'Paired again fixture'); await sample('after-new-pairing');
  check('new pairing receives a different computer identity',(await control('list')).pairedBrowsers.some(row=>row.id!==revokedId));
  await disconnect(true,'new pairing disconnect'); await noOwner();
  check('no uncaught production frontend exceptions',pageErrors.length===0);
  result={ok:true,browser:browserVersion,checks,media,api,cookieFlags,pageErrors,
    browserCertificateValidation:'Automation ignoreHTTPSErrors for isolated self-signed HTTPS only; no system trust changes',
    scope:fixture.authOnly
      ? 'Real branded-browser binary, production HTTPS gateway/frontend authentication, persistent browser process and gateway process restarts. Capture is intentionally unavailable in this separate fixture; no personal desktop/input and no HTTP mocks. No PWA installation.'
      : 'Real production HTTPS/frontend/native capture. Persistent browser process and gateway process restarts. Private Xvfb/Pulse and native input disabled. No HTTP mocks; no physical iPad or compositor display restoration.'};
} catch(error) {
  let state={};
  if(page) try {state=await page.evaluate(()=>({phase:document.getElementById('connection-status')?.dataset.state,paired:document.body.dataset.paired,notice:document.getElementById('notice')?.textContent}));} catch(_) {}
  result={ok:false,error:error.message,state,browser:browserVersion,checks,media,api,cookieFlags,pageErrors};
  process.exitCode=1;
} finally {
  await close();
  await writeFile(path.join(output,'pairing-result.json'),JSON.stringify(result,null,2));
  console.log(JSON.stringify({ok:result.ok,checks:checks.length,error:result.error,output:path.join(output,'pairing-result.json')}));
}
