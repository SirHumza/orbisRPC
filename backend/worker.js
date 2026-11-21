/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
const API = 'https://discord.com/api/v10';
const SCOPE = 'openid sdk.social_layer_presence';
const IDLE_MS = 30 * 86400 * 1000;
const LEASE_RENEWAL_MS = 86400 * 1000;

function json(body, status = 200, headers = {}) {
  return new Response(JSON.stringify(body), {
    status,
    headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store', ...headers }
  });
}
function error(code, message, status, headers, interval) {
  return json({ error: { code, message }, ...(interval ? { interval } : {}) }, status, headers);
}
async function keyOf(secret) {
  const bytes = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(secret));
  return Array.from(new Uint8Array(bytes), b => b.toString(16).padStart(2, '0')).join('');
}
function randomSecret() {
  return Array.from(crypto.getRandomValues(new Uint8Array(32)), b => b.toString(16).padStart(2, '0')).join('');
}
function positiveSeconds(value) {
  return Number.isSafeInteger(value) && value > 0 && value <= 366 * 86400;
}
function allowedVerificationUrl(value) {
  try {
    const url = new URL(value);
    return url.protocol === 'https:' && url.hostname === 'discord.com' && url.pathname === '/activate';
  } catch { return false; }
}

export default {
  async fetch(request, env) {
    try {
      return await this.route(request, env);
    } catch {
      return error('unavailable', 'Authorization service unavailable.', 503);
    }
  },
  async route(request, env) {
    const path = new URL(request.url).pathname;
    if (!env.DISCORD_CLIENT_ID || !env.DISCORD_CLIENT_SECRET) {
      return error('not_configured', 'Discord application credentials are required.', 503);
    }
    if (path === '/v1/pair' && request.method === 'POST') {
      const limited = await env.PAIR_LIMIT.limit({ key: request.headers.get('CF-Connecting-IP') || 'unknown' });
      if (!limited.success) return error('rate_limited', 'Try pairing again later.', 429, { 'Retry-After': '60' }, 60);
      const secret = randomSecret();
      const stub = env.SESSIONS.get(env.SESSIONS.idFromName(await keyOf(secret)));
      const result = await stub.fetch(new Request('https://session/init', { method: 'POST' }));
      if (!result.ok) return result;
      return json({ ...await result.json(), session_token: secret }, 201);
    }
    if ((path !== '/v1/token' || request.method !== 'POST') &&
        (path !== '/v1/session' || request.method !== 'DELETE')) {
      return error('not_found', 'Endpoint not found.', 404);
    }
    const match = /^Bearer ([a-f0-9]{64})$/.exec(request.headers.get('Authorization') || '');
    if (!match) return error('unauthorized', 'A broker session is required.', 401);
    const stub = env.SESSIONS.get(env.SESSIONS.idFromName(await keyOf(match[1])));
    return stub.fetch(new Request(`https://session${path === '/v1/token' ? '/token' : '/revoke'}`, {
      method: request.method
    }));
  }
};

export class AuthSession {
  constructor(state, env) {
    this.state = state;
    this.env = env;
    this.queue = Promise.resolve();
  }
  // Serialize the complete network exchange as refresh tokens rotate on use.
  fetch(request) {
    const next = this.queue.then(() => this.handle(request));
    this.queue = next.catch(() => {});
    return next.catch(() => error('unavailable', 'Authorization service unavailable.', 503));
  }
  alarm() {
    const next = this.queue.then(async () => {
      const session = await this.state.storage.get('session');
      if (session && session.cleanupAt > Date.now()) {
        await this.state.storage.setAlarm(session.cleanupAt);
      } else {
        await this.state.storage.deleteAll();
      }
    });
    this.queue = next.catch(() => {});
    return next;
  }
  async discord(path, params) {
    const response = await fetch(`${API}${path}`, {
      method: 'POST', redirect: 'error', signal: AbortSignal.timeout(15000),
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ ...params, client_id: this.env.DISCORD_CLIENT_ID,
        client_secret: this.env.DISCORD_CLIENT_SECRET })
    });
    const body = path === '/oauth2/token/revoke' ? {} : await response.json();
    return { status: response.status, ok: response.ok, body, retryAfter: response.headers.get('Retry-After') };
  }
  async handle(request) {
    const path = new URL(request.url).pathname;
    const now = Date.now();
    if (path === '/init') {
      const result = await this.discord('/oauth2/device/authorize', { scope: SCOPE });
      if (!result.ok) return error('discord_authorization_failed', 'Discord could not start authorization.', 502);
      const b = result.body;
      if (typeof b.device_code !== 'string' || !b.device_code || typeof b.user_code !== 'string' ||
          !positiveSeconds(b.expires_in) || !positiveSeconds(b.interval) || !allowedVerificationUrl(b.verification_uri) ||
          (b.verification_uri_complete && !allowedVerificationUrl(b.verification_uri_complete))) {
        return error('invalid_discord_response', 'Discord returned invalid authorization data.', 502);
      }
      await this.state.storage.put('session', { deviceCode: b.device_code,
        expiresAt: now + b.expires_in * 1000, cleanupAt: now + b.expires_in * 1000, interval: b.interval, nextPoll: now + b.interval * 1000 });
      await this.state.storage.setAlarm(now + b.expires_in * 1000);
      return json({ user_code: b.user_code, verification_uri: b.verification_uri,
        verification_uri_complete: b.verification_uri_complete || '', expires_in: b.expires_in, interval: b.interval });
    }
    const session = await this.state.storage.get('session');
    if (!session) return error('session_expired', 'Pair the console again.', 401);
    if (path === '/revoke') {
      // Keep a tombstone until upstream revocation succeeds, allowing safe retry.
      session.revoking = true;
      await this.state.storage.put('session', session);
      if (session.refreshToken) {
        try {
          const result = await this.discord('/oauth2/token/revoke', {
            token: session.refreshToken, token_type_hint: 'refresh_token'
          });
          if (!result.ok) return error('revocation_failed', 'Retry unlinking or revoke the app in Discord settings.', 502);
        } catch {
          return error('revocation_failed', 'Retry unlinking or revoke the app in Discord settings.', 503);
        }
      }
      await this.state.storage.deleteAll();
      return new Response(null, { status: 204, headers: { 'Cache-Control': 'no-store' } });
    }
    if (session.revoking) return error('authorization_lost', 'Session is being revoked. Retry unlinking.', 401);
    if (path !== '/token') return error('not_found', 'Endpoint not found.', 404);
    if (!session.refreshToken && now >= session.expiresAt) {
      await this.state.storage.deleteAll();
      return error('session_expired', 'Pair the console again.', 401);
    }
    if (session.accessToken && session.expiresAt - now > 120000) {
      // Coalesce idle lease writes to once per day while serving cached tokens.
      if (!session.cleanupAt || session.cleanupAt <= now + IDLE_MS - LEASE_RENEWAL_MS) {
        session.cleanupAt = now + IDLE_MS;
        await this.state.storage.put('session', session);
        await this.state.storage.setAlarm(session.cleanupAt);
      }
      return this.tokenResponse(session, now);
    }
    if (session.nextPoll > now) {
      const wait = Math.ceil((session.nextPoll - now) / 1000);
      return json({ status: 'authorization_pending', interval: wait }, 202, { 'Retry-After': String(wait) });
    }
    session.nextPoll = now + session.interval * 1000;
    await this.state.storage.put('session', session);
    const result = await this.discord('/oauth2/token', session.refreshToken ? {
      grant_type: 'refresh_token', refresh_token: session.refreshToken
    } : { grant_type: 'urn:ietf:params:oauth:grant-type:device_code', device_code: session.deviceCode });
    if (!result.ok) {
      if (result.body.error === 'authorization_pending') {
        return json({ status: 'authorization_pending', interval: session.interval }, 202);
      }
      if (result.body.error === 'slow_down' || result.status === 429) {
        const retry = Number(result.body.retry_after ?? result.retryAfter);
        const wait = Number.isFinite(retry) && retry > 0 ? Math.ceil(retry) : 0;
        session.interval = Math.max(session.interval + 5, wait);
        session.nextPoll = Date.now() + session.interval * 1000;
        await this.state.storage.put('session', session);
        return error('slow_down', 'Wait before polling again.', 429, { 'Retry-After': String(session.interval) }, session.interval);
      }
      if (['access_denied', 'expired_token', 'invalid_grant'].includes(result.body.error)) {
        await this.state.storage.deleteAll();
        return error('authorization_lost', 'Pair the console again.', 401);
      }
      return error('discord_unavailable', 'Discord authorization unavailable.', 503);
    }
    const b = result.body;
    const scopes = typeof b.scope === 'string' ? b.scope.split(' ') : [];
    if (b.token_type?.toLowerCase() !== 'bearer' || typeof b.access_token !== 'string' || !b.access_token ||
        typeof b.refresh_token !== 'string' || !b.refresh_token || !positiveSeconds(b.expires_in) ||
        !scopes.includes('sdk.social_layer_presence') || !scopes.includes('openid')) {
      // A successful refresh rotates both tokens. Never reuse an old pair afterward.
      await this.state.storage.deleteAll();
      return error('invalid_discord_response', 'Discord returned invalid token data. Pair again.', 502);
    }
    const updated = { accessToken: b.access_token, refreshToken: b.refresh_token, scope: b.scope,
      expiresAt: Date.now() + b.expires_in * 1000, cleanupAt: Date.now() + IDLE_MS, interval: 5, nextPoll: 0 };
    await this.state.storage.put('session', updated);
    await this.state.storage.setAlarm(Date.now() + IDLE_MS);
    return this.tokenResponse(updated, Date.now());
  }
  tokenResponse(session, now) {
    return json({ access_token: session.accessToken, token_type: 'Bearer',
      expires_in: Math.max(0, Math.floor((session.expiresAt - now) / 1000)), scope: session.scope });
  }
}
