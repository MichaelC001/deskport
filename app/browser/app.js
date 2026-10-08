/* DeskPort browser client. Pairing credentials stay in HttpOnly cookies; CSRF tokens stay in memory. */
(() => {
  'use strict';

  const $ = id => document.getElementById(id);
  const ui = Object.fromEntries([
    'login-panel', 'login-form', 'access-code', 'connect', 'quality', 'request-audio',
    'host-name', 'connection-status', 'disconnect', 'session-panel', 'viewport',
    'desktop-video', 'video-placeholder', 'resume-audio', 'touch-mode', 'left-click',
    'right-click', 'keyboard-toggle', 'shortcuts-toggle', 'shortcuts-panel', 'text-panel',
    'remote-text', 'paste-text', 'sound-toggle', 'fullscreen-toggle', 'input-hint',
    'stream-info', 'notice', 'login-intro', 'credential-fields', 'remember-browser', 'device-name',
    'device-name-label', 'paired-ready', 'paired-name', 'forget-pairing', 'retry-pairing',
    'display-mode', 'desktop-zoom', 'session-zoom', 'show-controls'
  ].map(id => [id, $(id)]));
  const video = ui['desktop-video'];
  let token = '';
  let phase = 'idle';
  let generation = 0;
  let pairing = null;
  let restoreFailed = false;
  // Set when the computer confirms this browser runs on a Tailscale device owned
  // by the same user; such a browser connects without the access code.
  let tailnet = false;
  let authTail = Promise.resolve();
  let ending = null;
  let ownsMedia = false;
  let pendingStart = null;
  let pageLeaving = false;
  let pc = null;
  let channel = null;
  let heartbeatTimer = 0;
  let connectionTimer = 0;
  let lostTimer = 0;
  let noticeTimer = 0;
  let statsTimer = 0;
  let heartbeatBusy = false;
  let heartbeatFailures = 0;
  let wantAudio = false;
  let inputQueue = [];
  let inputDraining = false;
  let composing = false;
  let wheelX = 0;
  let wheelY = 0;
  let gesture = null;
  // Host-reported features; older hosts report none and keep a fixed desktop.
  let capabilities = {};
  let sizeTimer = 0;
  let sentSize = '';
  let resizing = null;
  let controlsTimer = 0;
  const pointers = new Map();
  const pressedKeys = new Set();
  const pressedButtons = new Set();
  const errorMessages = {
    unauthorized: '访问码不正确或本次连接已过期，请重新输入。',
    unpaired: '此浏览器尚未配对，或配对已被电脑取消。请重新输入访问码。',
    'storage-unavailable': '电脑暂时无法保存配对变更，请稍后重试。',
    'rate-limited': '尝试次数过多，请稍后再试。',
    'sharing-disabled': '电脑尚未开启屏幕共享，请在电脑端开启后重试。',
    busy: '电脑正被其他连接使用，请结束原连接后再试。',
    unsupported: '这台电脑暂时无法提供浏览器串流，请检查电脑端状态。',
    'unsupported-codec': '当前浏览器不支持所需的视频格式（H.264 Baseline 3.1），请更新或更换浏览器。',
    'media-unavailable': '电脑当前无法采集或编码画面，请检查电脑端共享权限与编码器状态。',
    'invalid-request': '连接请求未被电脑接受，请刷新页面后重试。',
    timeout: '连接超时，请检查电脑是否在线以及网络是否畅通。',
    network: '无法连接电脑，请检查网络和电脑端的 HTTPS 证书。',
    'tailnet-denied': '这台 Tailscale 设备不属于这台电脑的用户，请输入访问码。',
    'tailnet-unavailable': '当前网络需要输入电脑端显示的访问码。'
  };

  function notice(message, error = false, persistent = false) {
    clearTimeout(noticeTimer);
    ui.notice.textContent = message;
    ui.notice.dataset.error = String(error);
    ui.notice.hidden = !message;
    if (message && !persistent) noticeTimer = setTimeout(() => { ui.notice.hidden = true; }, 7000);
  }

  function setPhase(next) {
    phase = next;
    ui['connection-status'].textContent = {
      idle: pairing ? '已配对' : tailnet ? '已通过 Tailscale 确认' : '未连接', restoring: '正在检查配对', forgetting: '正在取消配对',
      authenticating: '正在验证', connecting: '正在连接', streaming: '已连接', stopping: '正在断开'
    }[next];
    ui['connection-status'].dataset.state = next;
    const sessionVisible = next === 'connecting' || next === 'streaming';
    ui['login-panel'].hidden = sessionVisible;
    ui['session-panel'].hidden = !sessionVisible;
    ui.disconnect.hidden = !sessionVisible;
    document.body.dataset.session = String(sessionVisible);
    ui.connect.disabled = next !== 'idle' || restoreFailed;
    ui['access-code'].disabled = next !== 'idle' || Boolean(pairing) || tailnet || restoreFailed;
    ui['access-code'].required = !pairing && !tailnet && !restoreFailed;
    ui['credential-fields'].hidden = Boolean(pairing) || tailnet || restoreFailed;
    ui['paired-ready'].hidden = !pairing;
    ui['paired-name'].textContent = pairing?.name || '此浏览器';
    ui['forget-pairing'].disabled = next !== 'idle';
    ui['retry-pairing'].hidden = !restoreFailed;
    ui['retry-pairing'].disabled = next !== 'idle';
    ui['remember-browser'].disabled = next !== 'idle';
    ui['device-name'].disabled = next !== 'idle' || !ui['remember-browser'].checked;
    document.body.dataset.paired = String(Boolean(pairing));
    ui['login-intro'].textContent = restoreFailed ? '暂时无法确认配对状态。网络恢复后，请重新检查。'
      : pairing ? '此浏览器已与电脑配对。点击连接即可使用。'
        : tailnet ? '已通过 Tailscale 确认这是你的设备，无需访问码。点击连接即可使用。' : '输入这台电脑上显示的 6 位访问码。';
    ui.disconnect.disabled = next === 'stopping';
    ui['video-placeholder'].hidden = next === 'streaming';
  }

  function serializeAuth(operation) {
    const result = authTail.then(operation, operation);
    authTail = result.catch(() => {});
    return result;
  }

  function acceptAuthentication(result) {
    if (typeof result.csrfToken !== 'string' || !result.csrfToken) throw new Error('电脑未返回有效的连接凭据，请刷新后重试。');
    token = result.csrfToken;
    pairing = result.paired ? { id: result.pairing?.id || '', name: result.pairing?.name || '此浏览器' } : null;
    restoreFailed = false;
  }

  async function restorePairing(announce = false) {
    const currentGeneration = ++generation;
    setPhase('restoring');
    try {
      await serializeAuth(async () => {
        if (currentGeneration !== generation) return;
        const result = await api('/api/resume', {}, { token: '' });
        if (currentGeneration === generation) acceptAuthentication(result);
      });
      if (currentGeneration === generation && announce) notice(pairing ? '配对已恢复，点击连接即可使用。' : '请输入电脑端访问码。');
    } catch (error) {
      if (currentGeneration !== generation) return;
      token = '';
      if (error.code === 'unpaired') {
        const wasPaired = Boolean(pairing);
        pairing = null;
        restoreFailed = false;
        await probeTailnet(currentGeneration);
        if ((wasPaired || announce) && !tailnet) notice(errorMessages.unpaired, true);
      } else {
        restoreFailed = true;
        notice(error.message, true, true);
      }
    } finally { if (currentGeneration === generation) setPhase('idle'); }
  }

  // Asks whether this connection comes from an owned Tailscale device. It never
  // creates a session; any failure keeps the ordinary access-code form.
  async function probeTailnet(currentGeneration) {
    try {
      const result = await api('/api/login/tailnet', { probe: true }, { token: '' });
      if (currentGeneration === generation) tailnet = result.tailnet === true;
    } catch {
      if (currentGeneration === generation) tailnet = false;
    }
  }

  function defaultDeviceName() {
    const agent = navigator.userAgent;
    const device = /iPad/.test(agent) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1) ? 'iPad'
      : /iPhone/.test(agent) ? 'iPhone' : /Android/.test(agent) ? 'Android' : /Windows/.test(agent) ? 'Windows'
        : /Mac/.test(agent) ? 'Mac' : /Linux/.test(agent) ? 'Linux' : '';
    const browser = /Edg/.test(agent) ? 'Edge' : /Firefox|FxiOS/.test(agent) ? 'Firefox'
      : /Chrome|CriOS/.test(agent) ? 'Chrome' : /Safari/.test(agent) ? 'Safari' : '浏览器';
    return [device, browser].filter(Boolean).join(' ');
  }

  async function api(path, body, options = {}) {
    const headers = { 'Accept': 'application/json' };
    const requestToken = options.token === undefined ? token : options.token;
    if (requestToken) headers['X-DeskPort-Session'] = requestToken;
    if (body !== undefined) headers['Content-Type'] = 'application/json';
    const abort = new AbortController();
    const timer = setTimeout(() => abort.abort(), options.timeout || 15000);
    try {
      const response = await fetch(path, {
        method: body === undefined ? 'GET' : 'POST', credentials: 'same-origin', cache: 'no-store',
        headers, body: body === undefined ? undefined : JSON.stringify(body),
        signal: abort.signal, keepalive: Boolean(options.keepalive)
      });
      let data;
      try { data = await response.json(); } catch (_) { data = {}; }
      if (!response.ok || data.ok === false || data.status === false) {
        const code = data.code || (response.status === 401 ? 'unauthorized' : response.status === 429 ? 'rate-limited' : 'invalid-request');
        const error = new Error(errorMessages[code] || data.message || '电脑未能完成此操作。');
        error.code = code;
        throw error;
      }
      return data;
    } catch (error) {
      if (error.code) throw error;
      const wrapped = new Error(errorMessages[error.name === 'AbortError' ? 'timeout' : 'network']);
      wrapped.code = error.name === 'AbortError' ? 'timeout' : 'network';
      throw wrapped;
    } finally { clearTimeout(timer); }
  }

  // The page reports its drawable area; the computer's shared workspace policy
  // chooses the desktop size, backing scale and minimums from it.
  function viewportSize() {
    const rect = ui.viewport.getBoundingClientRect();
    const width = Math.round(rect.width) || window.innerWidth;
    const height = Math.round(rect.height) || Math.max(240, window.innerHeight - 170);
    const ratio = Math.round(Math.max(0.5, Math.min(8, window.devicePixelRatio || 1)) * 100) / 100;
    return { width: Math.max(160, width), height: Math.max(120, height), ratio };
  }
  function layoutOptions() {
    return { viewport: viewportSize(), zoom: Number(ui['session-zoom'].value || ui['desktop-zoom'].value || 1) };
  }
  function layoutKey(options) {
    return `${options.viewport.width}x${options.viewport.height}@${options.viewport.ratio}/${options.zoom}`;
  }
  function fillZoom(choices) {
    const values = Array.isArray(choices) && choices.length ? choices : [1];
    const current = Number(ui['desktop-zoom'].value || localStorageGet('deskport.zoom') || 1);
    for (const select of [ui['desktop-zoom'], ui['session-zoom']]) {
      select.replaceChildren(...values.map(value => {
        const option = document.createElement('option');
        option.value = String(value);
        option.textContent = `${Math.round(value * 100)}%`;
        return option;
      }));
      select.value = String(values.includes(current) ? current : 1);
    }
    ui['session-zoom'].parentElement.hidden = values.length < 2;
  }
  function localStorageGet(key) { try { return localStorage.getItem(key); } catch (_) { return null; } }
  function localStorageSet(key, value) { try { localStorage.setItem(key, value); } catch (_) { /* Optional. */ } }

  function scheduleResize(delay = 500) {
    clearTimeout(sizeTimer);
    sizeTimer = setTimeout(() => { void resizeDesktop(); }, delay);
  }
  async function resizeDesktop() {
    if (phase !== 'streaming' || !capabilities.resize || document.hidden) return;
    const options = layoutOptions();
    const key = layoutKey(options);
    if (key === sentSize) return;
    if (resizing) { scheduleResize(300); return; }
    const currentGeneration = generation;
    sentSize = key;
    resizing = api('/api/session/resize', options, { timeout: 15000 });
    try {
      const result = await resizing;
      if (currentGeneration === generation && result.superseded) scheduleResize(0);
    } catch (error) {
      if (currentGeneration !== generation) return;
      sentSize = '';
      notice(error.code === 'display-unavailable' ? '电脑暂时无法调整桌面尺寸，已保持当前画面。' : error.message, true);
    } finally { resizing = null; }
  }

  function streamOptions() {
    const quality = ui.quality.value;
    const maxWidth = 1280;
    const maxHeight = 720;
    // This is a browser stream-size hint; host shared-core policy remains authoritative.
    const ratio = Math.max(0.6, Math.min(3.2, window.innerWidth / Math.max(240, window.innerHeight - 170)));
    let width = Math.min(maxWidth, maxHeight * ratio);
    let height = width / ratio;
    if (width < 640) { width = 640; height = Math.min(maxHeight, width / ratio); }
    if (height < 360) { height = 360; width = Math.min(maxWidth, height * ratio); }
    const codecs = window.RTCRtpReceiver?.getCapabilities?.('video')?.codecs;
    const videoCapabilities = codecs?.filter(codec => codec.mimeType.toLowerCase() === 'video/h264')
      .map(({ mimeType, clockRate, sdpFmtpLine }) => ({ mimeType, clockRate, sdpFmtpLine }));
    if (codecs && !videoCapabilities.length) throw new Error('此浏览器没有可用的 H.264 解码器，请使用较新的 Safari、Chrome 或 Edge。');
    return {
      width: Math.floor(width / 4) * 4, height: Math.floor(height / 4) * 4,
      fps: 30, bitrateKbps: quality === 'smooth' ? 4500 : quality === 'sharp' ? 12000 : 8000,
      audio: wantAudio, input: true, videoCapabilities,
      ...(capabilities.resize ? { ...layoutOptions(), displayPolicy: Number(ui['display-mode'].value) } : {})
    };
  }

  function waitForIce(peer, currentGeneration) {
    if (peer.iceGatheringState === 'complete') return Promise.resolve();
    return new Promise((resolve, reject) => {
      const finish = error => {
        clearTimeout(timer);
        peer.removeEventListener('icegatheringstatechange', changed);
        if (error) reject(error); else resolve();
      };
      const changed = () => {
        if (currentGeneration !== generation || peer.signalingState === 'closed') finish(new Error('连接已取消。'));
        else if (peer.iceGatheringState === 'complete') finish();
      };
      const timer = setTimeout(() => finish(new Error('浏览器未能完成网络协商，请检查网络后重试。')), 12000);
      peer.addEventListener('icegatheringstatechange', changed);
      changed();
    });
  }

  async function connect(event) {
    event.preventDefault();
    if (phase !== 'idle') return;
    if (!window.isSecureContext || location.protocol !== 'https:') {
      notice('请通过受信任的 HTTPS 地址打开此页面。当前浏览器安全环境无法建立连接。', true, true);
      return;
    }
    if (!window.RTCPeerConnection) {
      notice('此浏览器不支持 WebRTC，请使用较新的 Safari、Chrome 或 Edge。', true, true);
      return;
    }
    const code = ui['access-code'].value.trim();
    if (!pairing && !tailnet && !/^[a-z0-9]{6}$/i.test(code)) { notice('请输入电脑端显示的 6 位字母数字访问码。', true); return; }
    const currentGeneration = ++generation;
    wantAudio = ui['request-audio'].checked;
    // Prime playback inside the user gesture; a visible fallback handles Safari rejecting it.
    video.muted = !wantAudio;
    video.play().catch(() => {});
    notice('');
    setPhase('authenticating');
    try {
      await serializeAuth(async () => {
        if (currentGeneration !== generation) return;
        const login = pairing ? await api('/api/resume', {}, { token: '' })
          : tailnet ? await api('/api/login/tailnet', {}, { token: '' })
          : await api('/api/login', {
            code, remember: ui['remember-browser'].checked,
            deviceName: ui['device-name'].value.replace(/[\u0000-\u001f\u007f]/g, '').trim().slice(0, 64) || defaultDeviceName()
          }, { token: '' });
        if (currentGeneration === generation) acceptAuthentication(login);
      });
      ui['access-code'].value = '';
      if (currentGeneration !== generation) return;
      const status = await api('/api/status');
      if (currentGeneration !== generation) return;
      ui['host-name'].textContent = status.hostName || '远程电脑';
      if (status.sharing === false) throw new Error(errorMessages['sharing-disabled']);
      if (status.mediaAvailable === false) throw new Error(errorMessages.unsupported);
      if (status.busy) throw new Error(errorMessages.busy);
      capabilities = status.capabilities || {};
      fillZoom(capabilities.zoomChoices);
      if (ui['display-mode'].value !== '0' && !capabilities.extendDisplay) {
        ui['display-mode'].value = '0';
        notice('这台电脑暂不支持扩展显示器，将调整电脑桌面以适合此窗口。');
      }
      setPhase('connecting');
      const peer = new RTCPeerConnection({ iceServers: [], bundlePolicy: 'max-bundle' });
      pc = peer;
      const stream = new MediaStream();
      video.srcObject = stream;
      peer.ontrack = ({ track }) => {
        if (currentGeneration !== generation) return;
        stream.addTrack(track);
        void playRemote();
        track.addEventListener('ended', () => {
          if (track.kind === 'video' && currentGeneration === generation) failSession('电脑已停止发送画面，请重新连接。');
        });
      };
      peer.ondatachannel = ({ channel: incoming }) => {
        if (currentGeneration !== generation || !['input', 'control'].includes(incoming.label)) { incoming.close(); return; }
        channel = incoming;
        incoming.onopen = () => { if (currentGeneration === generation) sendInput({ type: 'release' }, true); };
        incoming.onclose = () => { if (channel === incoming) channel = null; };
        incoming.onerror = () => { if (channel === incoming) channel = null; };
      };
      peer.onconnectionstatechange = () => {
        if (currentGeneration !== generation) return;
        clearTimeout(lostTimer);
        if (peer.connectionState === 'failed' || peer.connectionState === 'closed') failSession('串流连接已断开，请重新连接。');
        else if (peer.connectionState === 'disconnected') {
          releaseInput();
          notice('网络暂时中断，正在等待连接恢复…');
          lostTimer = setTimeout(() => { if (currentGeneration === generation) failSession('网络连接未能恢复，请重新连接。'); }, 8000);
        } else if (peer.connectionState === 'connected') notice('');
      };
      const reservation = { token, succeeded: false, promise: null };
      const startOptions = streamOptions();
      sentSize = startOptions.viewport ? layoutKey(startOptions) : '';
      reservation.promise = api('/api/session/start', startOptions, { timeout: 30000 }).then(offer => {
        reservation.succeeded = true;
        return offer;
      });
      pendingStart = reservation;
      const offer = await reservation.promise;
      if (pendingStart === reservation) pendingStart = null;
      if (currentGeneration !== generation) return;
      ownsMedia = true;
      if (offer.type !== 'offer' || typeof offer.sdp !== 'string') throw new Error('电脑返回的媒体协商信息无效。');
      await peer.setRemoteDescription({ type: 'offer', sdp: offer.sdp });
      // Receive desktop audio/video only; no camera or microphone permissions are requested.
      for (const transceiver of peer.getTransceivers()) {
        if (transceiver.receiver.track?.kind === 'video' || transceiver.receiver.track?.kind === 'audio') transceiver.direction = 'recvonly';
      }
      await peer.setLocalDescription(await peer.createAnswer());
      await waitForIce(peer, currentGeneration);
      if (currentGeneration !== generation) return;
      await api('/api/session/answer', { type: 'answer', sdp: peer.localDescription.sdp });
      if (currentGeneration !== generation) return;
      heartbeatFailures = 0;
      heartbeatTimer = setInterval(heartbeat, 5000);
      statsTimer = setInterval(updateStats, 2500);
      connectionTimer = setTimeout(() => {
        if (currentGeneration === generation && phase !== 'streaming') failSession('媒体连接未建立，请检查电脑防火墙与局域网连接后重试。');
      }, 20000);
    } catch (error) {
      ui['access-code'].value = '';
      if (currentGeneration === generation) {
        if (error.code === 'unpaired') { token = ''; pairing = null; restoreFailed = false; }
        // Ownership can change (device retagged or removed); fall back to the code.
        if (error.code === 'tailnet-denied' || error.code === 'tailnet-unavailable') tailnet = false;
        await endSession(error.message, true);
      }
    }
  }

  async function heartbeat() {
    if (!token || heartbeatBusy || !['connecting', 'streaming'].includes(phase)) return;
    heartbeatBusy = true;
    const currentGeneration = generation;
    try {
      await api('/api/session/heartbeat', {}, { timeout: 7000 });
      if (currentGeneration === generation) heartbeatFailures = 0;
    } catch (error) {
      if (currentGeneration === generation && (error.code === 'unauthorized' || error.code === 'unpaired' || ++heartbeatFailures >= 2)) {
        await endSession(error.message, true);
        if (pairing && (error.code === 'unauthorized' || error.code === 'unpaired')) await restorePairing(true);
      }
    } finally { heartbeatBusy = false; }
  }

  function clearLocalSession() {
    clearInterval(heartbeatTimer);
    clearInterval(statsTimer);
    clearTimeout(connectionTimer);
    clearTimeout(lostTimer);
    clearTimeout(sizeTimer);
    clearTimeout(controlsTimer);
    heartbeatTimer = statsTimer = connectionTimer = lostTimer = sizeTimer = controlsTimer = 0;
    sentSize = '';
    resetGesture();
    inputQueue = [];
    pressedKeys.clear();
    pressedButtons.clear();
    channel = null;
    const peer = pc;
    pc = null;
    if (peer) { peer.onconnectionstatechange = peer.ontrack = peer.ondatachannel = null; peer.close(); }
    video.pause();
    video.srcObject = null;
    ui['resume-audio'].hidden = true;
    ui['stream-info'].textContent = '';
    ui['remote-text'].value = '';
    ui['text-panel'].hidden = ui['shortcuts-panel'].hidden = true;
    ui['keyboard-toggle'].setAttribute('aria-expanded', 'false');
    ui['shortcuts-toggle'].setAttribute('aria-expanded', 'false');
  }

  async function endSession(message = '已断开连接。需要时可再次连接。', error = false, unloading = false) {
    if (ending) return ending;
    const oldToken = token;
    const ownedMedia = ownsMedia;
    const wasPaired = Boolean(pairing);
    const reservation = pendingStart;
    pendingStart = null;
    ownsMedia = false;
    releaseInput();
    const currentGeneration = ++generation;
    token = '';
    setPhase('stopping');
    clearLocalSession();
    ui['access-code'].value = '';
    ending = (async () => {
      // Serialize cookie-mutating auth calls so an old logout cannot invalidate a new resume.
      await serializeAuth(async () => {
        // Keep Connect disabled until a cancelled start resolves. Only a successful
        // reservation belongs to this page; a busy rejection must not stop another tab.
        if (reservation) { try { await reservation.promise; } catch (_) { /* No reservation. */ } }
        const cleanupToken = reservation?.token || oldToken;
        if (currentGeneration !== generation || !cleanupToken || !(ownedMedia || reservation?.succeeded)) return;
        try { await api('/api/session/stop', {}, { token: cleanupToken, timeout: 4000, keepalive: unloading }); } catch (_) { /* Lease fallback. */ }
        // Paired tabs share a short session. A second tab may start after /stop,
        // so this page must never log out that tab's new stream.
        if (!wasPaired && !unloading && !pageLeaving && currentGeneration === generation) {
          try { await api('/api/logout', {}, { token: cleanupToken, timeout: 4000 }); } catch (_) { /* Pairing is retained. */ }
        }
      });
      if (currentGeneration !== generation) return;
      setPhase('idle');
      if (message) notice(message, error, error);
      if (!unloading) (pairing ? ui.connect : ui['access-code']).focus({ preventScroll: true });
    })();
    try { await ending; } finally { ending = null; }
  }

  async function forgetPairing() {
    if (phase !== 'idle' || !pairing) return;
    const currentGeneration = ++generation;
    setPhase('forgetting');
    notice('');
    try {
      await serializeAuth(async () => {
        if (currentGeneration !== generation) return;
        // Resume when the preceding disconnect cleared the short session. A 401 session
        // expiry also gets one fresh resume; only /forget success removes local pairing UI.
        if (!token) {
          const result = await api('/api/resume', {}, { token: '' });
          if (currentGeneration !== generation) return;
          acceptAuthentication(result);
        }
        try { await api('/api/forget', {}); }
        catch (error) {
          if (error.code !== 'unauthorized') throw error;
          const result = await api('/api/resume', {}, { token: '' });
          if (currentGeneration !== generation) return;
          acceptAuthentication(result);
          await api('/api/forget', {});
        }
        if (currentGeneration !== generation) return;
        token = ''; pairing = null; restoreFailed = false;
        clearLocalSession();
        ui['access-code'].value = '';
      });
      if (currentGeneration === generation) notice('已取消此浏览器配对。下次连接需要重新输入访问码。');
    } catch (error) {
      if (currentGeneration !== generation) return;
      if (error.code === 'unpaired') {
        token = ''; pairing = null; restoreFailed = false;
        notice('电脑已取消此浏览器配对，请重新输入访问码。', true);
      } else notice('未能确认取消配对。' + error.message, true, true);
    } finally { if (currentGeneration === generation) setPhase('idle'); }
  }

  async function failSession(message) {
    const currentGeneration = generation;
    await endSession(message, true);
    // A closed media connection can also mean the computer revoked this pairing.
    // Recheck credentials without automatically starting another desktop session.
    if (pairing && generation === currentGeneration + 1 && phase === 'idle') await restorePairing();
  }

  function sendInput(event, allowConnecting = false) {
    if (!token || (phase !== 'streaming' && !(allowConnecting && phase === 'connecting'))) return false;
    if (channel?.readyState === 'open' && !inputDraining && !inputQueue.length) {
      if (channel.bufferedAmount >= 65536) {
        // Switching transports here could deliver a key-up before its queued key-down.
        if (event.type !== 'release') failSession('输入连接拥堵，已断开以释放远程按键，请重新连接。');
        return false;
      }
      try { channel.send(JSON.stringify(event)); return true; } catch (_) { /* Use the authenticated HTTP fallback. */ }
    }
    // Keep HTTP input ordered and bounded. Old pointer movement may be superseded, keys may not.
    const previous = inputQueue[inputQueue.length - 1];
    if (event.type === 'release') inputQueue = [event];
    else if (event.type === 'move' && previous?.type === 'move') inputQueue[inputQueue.length - 1] = event;
    else if (event.type === 'relative' && previous?.type === 'relative') {
      previous.dx = Math.max(-32767, Math.min(32767, previous.dx + event.dx));
      previous.dy = Math.max(-32767, Math.min(32767, previous.dy + event.dy));
    } else if (inputQueue.length < 64) inputQueue.push(event);
    else { inputQueue = [{ type: 'release' }]; notice('输入网络拥堵，已释放按键，请稍后再操作。', true); }
    void drainInput();
    return true;
  }

  async function drainInput() {
    if (inputDraining) return;
    inputDraining = true;
    const currentGeneration = generation;
    try {
      while (inputQueue.length && currentGeneration === generation && token) {
        await api('/api/session/input', { event: inputQueue.shift() }, { timeout: 4000 });
      }
    } catch (error) {
      inputQueue = [];
      if (currentGeneration === generation) failSession('远程输入连接已中断。' + error.message);
    } finally {
      inputDraining = false;
      if (inputQueue.length && token) void drainInput();
    }
  }

  function releaseInput() {
    sendInput({ type: 'release' }, true);
    pressedKeys.clear();
    pressedButtons.clear();
    resetGesture();
  }

  function button(number, down) {
    if (down) pressedButtons.add(number); else pressedButtons.delete(number);
    sendInput({ type: 'button', button: number, down });
  }

  function click(number) { button(number, true); button(number, false); }

  function videoPosition(clientX, clientY) {
    const rect = ui.viewport.getBoundingClientRect();
    const width = video.videoWidth || 1920;
    const height = video.videoHeight || 1080;
    const scale = Math.min(rect.width / width, rect.height / height);
    const left = rect.left + (rect.width - width * scale) / 2;
    const top = rect.top + (rect.height - height * scale) / 2;
    return {
      type: 'move', x: Math.max(0, Math.min(width - 1, Math.round((clientX - left) / scale))),
      y: Math.max(0, Math.min(height - 1, Math.round((clientY - top) / scale))), width, height
    };
  }

  function resetGesture() {
    if (gesture) clearTimeout(gesture.holdTimer);
    gesture = null;
    pointers.clear();
    wheelX = wheelY = 0;
  }

  function scrollPixels(dx, dy) {
    // DOM positive Y scrolls down; DeskPort positive Y scrolls up. One CSS pixel = one wheel unit.
    wheelX += dx;
    wheelY -= dy;
    const x = Math.trunc(wheelX);
    const y = Math.trunc(wheelY);
    if (x || y) {
      sendInput({ type: 'scroll', x: Math.max(-32767, Math.min(32767, x)), y: Math.max(-32767, Math.min(32767, y)) });
      wheelX -= x;
      wheelY -= y;
    }
  }

  ui.viewport.addEventListener('pointerdown', event => {
    if (phase !== 'streaming' || event.target === ui['resume-audio']) return;
    event.preventDefault();
    ui.viewport.focus({ preventScroll: true });
    ui.viewport.setPointerCapture(event.pointerId);
    if (event.pointerType !== 'touch') {
      sendInput(videoPosition(event.clientX, event.clientY));
      const number = [1, 2, 3, 4, 5][event.button];
      if (number) button(number, true);
      return;
    }
    pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
    if (!gesture) {
      gesture = { x: event.clientX, y: event.clientY, started: performance.now(), moved: false, multi: false, dragging: false, holdTimer: 0 };
      if (ui['touch-mode'].value === 'direct') {
        sendInput(videoPosition(event.clientX, event.clientY));
        button(1, true);
        gesture.dragging = true;
      } else {
        gesture.holdTimer = setTimeout(() => {
          if (gesture && !gesture.moved && !gesture.multi && pointers.size === 1) { button(1, true); gesture.dragging = true; }
        }, 450);
      }
    } else {
      clearTimeout(gesture.holdTimer);
      gesture.multi = true;
      if (gesture.dragging) { button(1, false); gesture.dragging = false; }
    }
  });

  ui.viewport.addEventListener('pointermove', event => {
    if (phase !== 'streaming') return;
    if (event.pointerType !== 'touch') { sendInput(videoPosition(event.clientX, event.clientY)); return; }
    const previous = pointers.get(event.pointerId);
    if (!previous || !gesture) return;
    event.preventDefault();
    const dx = event.clientX - previous.x;
    const dy = event.clientY - previous.y;
    pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
    if (Math.hypot(event.clientX - gesture.x, event.clientY - gesture.y) > 6) { gesture.moved = true; clearTimeout(gesture.holdTimer); }
    if (pointers.size > 1) scrollPixels(-dx * 1.5, -dy * 1.5);
    else if (!gesture.multi) {
      if (ui['touch-mode'].value === 'direct') sendInput(videoPosition(event.clientX, event.clientY));
      else if (dx || dy) sendInput({ type: 'relative', dx: Math.round(dx * 1.6), dy: Math.round(dy * 1.6) });
    }
  });

  function pointerUp(event) {
    if (event.pointerType !== 'touch') {
      const number = [1, 2, 3, 4, 5][event.button];
      if (number && pressedButtons.has(number)) button(number, false);
    } else if (gesture) {
      pointers.delete(event.pointerId);
      if (!pointers.size) {
        clearTimeout(gesture.holdTimer);
        if (gesture.dragging) button(1, false);
        else if (!gesture.moved && performance.now() - gesture.started < 450) click(gesture.multi ? 3 : 1);
        resetGesture();
      }
    }
    if (ui.viewport.hasPointerCapture(event.pointerId)) ui.viewport.releasePointerCapture(event.pointerId);
  }
  ui.viewport.addEventListener('pointerup', pointerUp);
  ui.viewport.addEventListener('pointercancel', releaseInput);
  ui.viewport.addEventListener('lostpointercapture', event => {
    if (pointers.has(event.pointerId) || pressedButtons.size) releaseInput();
  });
  ui.viewport.addEventListener('contextmenu', event => event.preventDefault());
  ui.viewport.addEventListener('wheel', event => {
    if (phase !== 'streaming') return;
    event.preventDefault();
    const multiplier = event.deltaMode === 1 ? 40 : event.deltaMode === 2 ? 480 : 1;
    scrollPixels(event.deltaX * multiplier, event.deltaY * multiplier);
  }, { passive: false });

  const keyCodes = {
    Backspace: 8, Tab: 9, Enter: 13, NumpadEnter: 13, ShiftLeft: 16, ShiftRight: 16,
    ControlLeft: 17, ControlRight: 17, AltLeft: 18, AltRight: 18, Pause: 19, CapsLock: 20,
    Escape: 27, Space: 32, PageUp: 33, PageDown: 34, End: 35, Home: 36,
    ArrowLeft: 37, ArrowUp: 38, ArrowRight: 39, ArrowDown: 40, PrintScreen: 44, Insert: 45,
    Delete: 46, MetaLeft: 91, MetaRight: 92, ContextMenu: 93, NumpadMultiply: 106,
    NumpadAdd: 107, NumpadSubtract: 109, NumpadDecimal: 110, NumpadDivide: 111,
    NumLock: 144, ScrollLock: 145, Semicolon: 186, Equal: 187, Comma: 188, Minus: 189,
    Period: 190, Slash: 191, Backquote: 192, BracketLeft: 219, Backslash: 220,
    BracketRight: 221, Quote: 222, IntlBackslash: 226
  };
  function virtualKey(code) {
    if (/^Key[A-Z]$/.test(code)) return code.charCodeAt(3);
    if (/^Digit[0-9]$/.test(code)) return code.charCodeAt(5);
    if (/^Numpad[0-9]$/.test(code)) return 96 + Number(code[6]);
    if (/^F([1-9]|1[0-9]|2[0-4])$/.test(code)) return 111 + Number(code.slice(1));
    return keyCodes[code];
  }
  const modifiers = event => (event.shiftKey ? 1 : 0) | (event.ctrlKey ? 2 : 0) | (event.altKey ? 4 : 0) | (event.metaKey ? 8 : 0);
  ui.viewport.addEventListener('keydown', event => {
    if (phase !== 'streaming' || event.isComposing || composing || event.keyCode === 229) return;
    const key = virtualKey(event.code);
    if (!key) return;
    event.preventDefault();
    pressedKeys.add(key);
    sendInput({ type: 'key', key, down: true, modifiers: modifiers(event) });
  });
  document.addEventListener('keyup', event => {
    const key = virtualKey(event.code);
    if (!pressedKeys.has(key)) return;
    event.preventDefault();
    pressedKeys.delete(key);
    sendInput({ type: 'key', key, down: false, modifiers: modifiers(event) });
  });
  ui.viewport.addEventListener('blur', releaseInput);
  ui['remote-text'].addEventListener('compositionstart', () => { composing = true; });
  ui['remote-text'].addEventListener('compositionend', () => { composing = false; });
  ui['text-panel'].addEventListener('submit', event => {
    event.preventDefault();
    if (composing) return;
    const text = ui['remote-text'].value;
    if (!text) return;
    if (new TextEncoder().encode(text).length > 4096) { notice('一次最多发送 4096 字节，请将文字分段发送。', true); return; }
    if (sendInput({ type: 'text', text })) { ui['remote-text'].value = ''; notice('文字已发送到电脑。'); }
  });
  ui['paste-text'].addEventListener('click', async () => {
    try {
      const text = await navigator.clipboard.readText();
      ui['remote-text'].value = text;
      ui['remote-text'].focus();
    } catch (_) { notice('请在文字框中长按并选择“粘贴”，或使用系统粘贴快捷键。'); ui['remote-text'].focus(); }
  });
  ui['shortcuts-panel'].addEventListener('click', event => {
    const buttonElement = event.target.closest('[data-keys]');
    if (!buttonElement) return;
    const keys = buttonElement.dataset.keys.split(',').map(Number);
    let flags = 0;
    const flag = key => ({ 16: 1, 17: 2, 18: 4, 91: 8, 92: 8 }[key] || 0);
    keys.forEach(key => { flags |= flag(key); sendInput({ type: 'key', key, down: true, modifiers: flags }); });
    keys.reverse().forEach(key => { flags &= ~flag(key); sendInput({ type: 'key', key, down: false, modifiers: flags }); });
    ui.viewport.focus({ preventScroll: true });
  });

  function updateSound() {
    ui['sound-toggle'].textContent = video.muted ? '声音关' : '声音开';
    ui['sound-toggle'].setAttribute('aria-pressed', String(!video.muted));
    if (!video.paused && (!video.muted || !wantAudio)) ui['resume-audio'].hidden = true;
  }
  async function playRemote() {
    const currentGeneration = generation;
    try { await video.play(); }
    catch (_) {
      if (currentGeneration !== generation) return;
      // Receive-only Safari sessions may reject unmuted autoplay. Keep video usable.
      video.muted = true;
      try { await video.play(); } catch (_) { /* The connection timeout supplies actionable feedback. */ }
      if (currentGeneration !== generation) return;
      ui['resume-audio'].hidden = !wantAudio;
    }
    if (currentGeneration !== generation) return;
    updateSound();
  }
  async function enableAudio() {
    if (!wantAudio) { notice('本次连接未启用电脑声音。请断开后在连接选项中开启。'); return; }
    video.muted = false;
    try { await video.play(); ui['resume-audio'].hidden = true; }
    catch (_) { ui['resume-audio'].hidden = false; notice('浏览器未允许播放声音，请再点按“开启声音”。'); }
    updateSound();
  }
  ui['resume-audio'].addEventListener('click', enableAudio);
  ui['sound-toggle'].addEventListener('click', () => {
    if (video.muted || video.paused) void enableAudio(); else { video.muted = true; updateSound(); }
  });
  video.addEventListener('playing', () => {
    if (!['connecting', 'streaming'].includes(phase) || !video.videoWidth) return;
    clearTimeout(connectionTimer);
    setPhase('streaming');
    // The page may have changed size while media was negotiating.
    scheduleResize(800);
    updateSound();
    ui.viewport.focus({ preventScroll: true });
  });
  video.addEventListener('resize', () => {
    if (video.videoWidth) ui['stream-info'].textContent = `${video.videoWidth} × ${video.videoHeight}`;
  });
  async function updateStats() {
    const peer = pc;
    if (!peer || phase !== 'streaming') return;
    try {
      const stats = await peer.getStats();
      if (peer !== pc) return;
      stats.forEach(report => {
        if (report.type === 'inbound-rtp' && (report.kind === 'video' || report.mediaType === 'video')) {
          const fps = report.framesPerSecond ? ` · ${Math.round(report.framesPerSecond)} 帧` : '';
          ui['stream-info'].textContent = `${video.videoWidth} × ${video.videoHeight}${fps}`;
        }
      });
    } catch (_) { /* Stats are optional and must not interrupt playback. */ }
  }

  ui['fullscreen-toggle'].addEventListener('click', async () => {
    try {
      releaseInput();
      if (document.fullscreenElement) await document.exitFullscreen();
      else if (ui['session-panel'].requestFullscreen) await ui['session-panel'].requestFullscreen();
      else notice('此浏览器不支持网页全屏。横向使用 iPad 可获得更大的画面。');
    } catch (_) { notice('浏览器未允许全屏，请保持页面前台后重试。'); }
  });
  // Fullscreen shows only the desktop; the toolbar returns on pointer movement
  // near the top edge or from the handle, and hides again after a pause.
  function showControls(sticky = false) {
    clearTimeout(controlsTimer);
    ui['session-panel'].dataset.controls = 'shown';
    ui['show-controls'].hidden = true;
    if (!sticky && document.fullscreenElement) controlsTimer = setTimeout(hideControls, 3000);
  }
  function hideControls() {
    if (!document.fullscreenElement) return;
    if (ui['session-panel'].querySelector('.toolbar:focus-within, .aux-panel:not([hidden])')) { showControls(); return; }
    ui['session-panel'].dataset.controls = 'hidden';
    ui['show-controls'].hidden = false;
  }
  document.addEventListener('fullscreenchange', () => {
    releaseInput();
    ui['fullscreen-toggle'].textContent = document.fullscreenElement ? '退出全屏' : '全屏';
    if (document.fullscreenElement) showControls(); else { clearTimeout(controlsTimer); delete ui['session-panel'].dataset.controls; ui['show-controls'].hidden = true; }
    scheduleResize(300);
  });
  ui['show-controls'].addEventListener('click', () => showControls());
  ui['session-panel'].addEventListener('pointermove', event => {
    if (document.fullscreenElement && event.pointerType === 'mouse' && event.clientY < 90) showControls();
  });
  for (const element of [ui['session-panel'].querySelector('.toolbar')]) {
    element.addEventListener('pointerenter', () => showControls(true));
    element.addEventListener('pointerleave', () => showControls());
  }
  if (window.ResizeObserver) new ResizeObserver(() => scheduleResize()).observe(ui.viewport);
  else window.addEventListener('resize', () => scheduleResize());
  for (const [source, other] of [['desktop-zoom', 'session-zoom'], ['session-zoom', 'desktop-zoom']]) {
    ui[source].addEventListener('change', () => {
      ui[other].value = ui[source].value;
      localStorageSet('deskport.zoom', ui[source].value);
      if (source === 'session-zoom') { releaseInput(); scheduleResize(0); }
    });
  }
  for (const [toggle, panel] of [['keyboard-toggle', 'text-panel'], ['shortcuts-toggle', 'shortcuts-panel']]) {
    ui[toggle].addEventListener('click', () => {
      releaseInput();
      ui[panel].hidden = !ui[panel].hidden;
      ui[toggle].setAttribute('aria-expanded', String(!ui[panel].hidden));
      if (panel === 'text-panel' && !ui[panel].hidden) ui['remote-text'].focus();
    });
  }
  ui['touch-mode'].addEventListener('change', () => {
    releaseInput();
    ui['input-hint'].textContent = ui['touch-mode'].value === 'direct'
      ? '点按选择 · 单指拖动 · 双指滚动'
      : '单指移动 · 点按左键 · 双指滚动 · 长按拖动';
  });
  ui['left-click'].addEventListener('click', () => click(1));
  ui['right-click'].addEventListener('click', () => click(3));
  ui['login-form'].addEventListener('submit', connect);
  ui['remember-browser'].addEventListener('change', () => {
    ui['device-name'].disabled = !ui['remember-browser'].checked;
  });
  ui['forget-pairing'].addEventListener('click', forgetPairing);
  ui['retry-pairing'].addEventListener('click', () => { void restorePairing(true); });
  ui.disconnect.addEventListener('click', () => { void endSession(); });
  window.addEventListener('blur', releaseInput);
  document.addEventListener('visibilitychange', () => {
    releaseInput();
    if (!document.hidden && phase === 'streaming') { void heartbeat(); void playRemote(); }
  });
  window.addEventListener('pagehide', () => { pageLeaving = true; void endSession('', false, true); });
  window.addEventListener('pageshow', event => {
    // A restored page is a new browser visit, even if the browser kept its JS heap in bfcache.
    if (event.persisted) { pageLeaving = false; ++generation; token = ''; ownsMedia = false; clearLocalSession(); ui['access-code'].value = ''; void restorePairing(); }
  });
  window.addEventListener('offline', () => { if (token) failSession('网络已断开。网络恢复后，请重新连接。'); });
  ui['device-name'].value = defaultDeviceName();
  fillZoom([1]);
  ui['display-mode'].value = localStorageGet('deskport.displayMode') === '2' ? '2' : '0';
  ui['display-mode'].addEventListener('change', () => localStorageSet('deskport.displayMode', ui['display-mode'].value));
  setPhase('idle');
  if (!window.isSecureContext || location.protocol !== 'https:') notice('请使用电脑端提供的 HTTPS 地址，并确认浏览器信任其证书。', true, true);
  else void restorePairing();
})();
