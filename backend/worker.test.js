/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
import test from 'node:test';
import assert from 'node:assert/strict';
import worker, { AuthSession } from './worker.js';
function fixture(initial) {
  const values = new Map(initial ? [['session', initial]] : []);
  const counts = { get: 0, put: 0, alarm: 0 };
  const storage = {
    get: async k => { counts.get++; return structuredClone(values.get(k)); },
    put: async (k, v) => { counts.put++; values.set(k, structuredClone(v)); },
    deleteAll: async () => values.clear(),
    setAlarm: async () => { counts.alarm++; }
  };
  return { values, counts, session: new AuthSession({ storage }, { DISCORD_CLIENT_ID: 'app', DISCORD_CLIENT_SECRET: 'secret' }) };
}
const request = () => new Request('https://session/token', { method: 'POST' });
const pending = () => ({ deviceCode: 'device', expiresAt: Date.now() + 600000, interval: 5, nextPoll: 0 });
const grant = (extra = {}) => ({ access_token: 'access', refresh_token: 'refresh', token_type: 'Bearer',
  expires_in: 3600, scope: 'openid sdk.social_layer_presence', ...extra });
test('device grant secrets remain on server', async () => {
  const f = fixture();
  globalThis.fetch = async () => Response.json({ device_code: 'private', user_code: 'code', verification_uri: 'https://discord.com/activate', expires_in: 600, interval: 5 });
  const response = await f.session.fetch(new Request('https://session/init', { method: 'POST' }));
  assert.equal(response.status, 200);
  assert.equal((await response.json()).device_code, undefined);
  assert.equal(f.values.get('session').deviceCode, 'private');
});
test('concurrent polls exchange once and omit refresh token', async () => {
  const f = fixture(pending()); let requests = 0;
  globalThis.fetch = async () => { requests++; await new Promise(resolve => setTimeout(resolve, 5)); return Response.json(grant()); };
  const responses = await Promise.all([f.session.fetch(request()), f.session.fetch(request())]);
  assert.equal(requests, 1);
  const body = await responses[0].json();
  assert.equal(body.refresh_token, undefined); assert.equal(body.access_token, 'access');
  assert.equal(responses[1].headers.get('Cache-Control'), 'no-store');
});
test('refresh rotation is serialized and persisted', async () => {
  const f = fixture({ ...pending(), accessToken: 'old', refreshToken: 'old-refresh', expiresAt: Date.now() + 1000 }); let requests = 0;
  globalThis.fetch = async (url, options) => { requests++; assert.equal(options.body.get('refresh_token'), 'old-refresh'); return Response.json(grant({ refresh_token: 'new-refresh' })); };
  await Promise.all([f.session.fetch(request()), f.session.fetch(request())]);
  assert.equal(requests, 1); assert.equal(f.values.get('session').refreshToken, 'new-refresh');
});
test('slow down enforces polling interval', async () => {
  const f = fixture(pending()); let requests = 0;
  globalThis.fetch = async () => { requests++; return Response.json({ error: 'slow_down' }, { status: 400 }); };
  const response = await f.session.fetch(request());
  assert.equal(response.status, 429); assert.equal(response.headers.get('Retry-After'), '10');
  assert.equal((await f.session.fetch(request())).status, 202); assert.equal(requests, 1);
});
test('invalid grant requires pairing again', async () => {
  const f = fixture(pending()); globalThis.fetch = async () => Response.json({ error: 'invalid_grant' }, { status: 400 });
  assert.equal((await f.session.fetch(request())).status, 401); assert.equal(f.values.size, 0);
});
test('wrong scope removes unusable rotated credentials', async () => {
  const f = fixture(pending()); globalThis.fetch = async () => Response.json(grant({ scope: 'identify' }));
  assert.equal((await f.session.fetch(request())).status, 502); assert.equal(f.values.size, 0);
});
test('transient refresh failure retains credentials for retry', async () => {
  const f = fixture({ ...pending(), refreshToken: 'existing-refresh' }); globalThis.fetch = async () => { throw new Error('offline'); };
  assert.equal((await f.session.fetch(request())).status, 503); assert.equal(f.values.get('session').refreshToken, 'existing-refresh');
});
test('revoke removes access and reports upstream failure', async () => {
  const f = fixture({ ...pending(), refreshToken: 'existing-refresh' }); globalThis.fetch = async () => Response.json({ error: 'server_error' }, { status: 500 });
  assert.equal((await f.session.fetch(new Request('https://session/revoke', { method: 'DELETE' }))).status, 502);
  assert.equal(f.values.get('session').revoking, true); assert.equal((await f.session.fetch(request())).status, 401);
  globalThis.fetch = async () => new Response(null, { status: 200 });
  assert.equal((await f.session.fetch(new Request('https://session/revoke', { method: 'DELETE' }))).status, 204);
  assert.equal(f.values.size, 0);
});
test('token request without broker session denied', async () => {
  const response = await worker.fetch(new Request('https://broker/v1/token', { method: 'POST' }), { DISCORD_CLIENT_ID: 'app', DISCORD_CLIENT_SECRET: 'secret' });
  assert.equal(response.status, 401);
});

test('old alarm preserves extended session deadline', async () => {
  const f = fixture({ ...pending(), cleanupAt: Date.now() + 86400000 });
  await f.session.alarm();
  assert.equal(f.values.size, 1);
});

test('Discord retry-after is respected and returned in JSON', async () => {
  const f = fixture(pending());
  globalThis.fetch = async () => Response.json({ retry_after: 60 }, { status: 429 });
  const response = await f.session.fetch(request());
  assert.equal(response.headers.get('Retry-After'), '60');
  assert.equal((await response.json()).interval, 60);
  assert.ok(f.values.get('session').nextPoll >= Date.now() + 59000);
});

test('pairing expiry alarm queued during successful exchange preserves new credentials', async () => {
  const f = fixture({ ...pending(), cleanupAt: Date.now() - 1 });
  let finish;
  const delayed = new Promise(resolve => { finish = resolve; });
  globalThis.fetch = async () => { await delayed; return Response.json(grant()); };
  const exchange = f.session.fetch(request());
  const alarm = f.session.alarm();
  finish();
  assert.equal((await exchange).status, 200);
  await alarm;
  assert.equal(f.values.get('session').accessToken, 'access');
});


test('one hundred cached token requests make no writes or upstream requests', async () => {
  const now = Date.now();
  const f = fixture({ accessToken: 'cached', refreshToken: 'refresh',
    scope: 'openid sdk.social_layer_presence', expiresAt: now + 7 * 86400000,
    cleanupAt: now + 30 * 86400000 });
  let upstream = 0;
  globalThis.fetch = async () => { upstream++; throw new Error('unexpected network call'); };
  for (let i = 0; i < 100; i++) {
    const response = await f.session.fetch(request());
    assert.equal(response.status, 200);
    assert.equal((await response.json()).access_token, 'cached');
  }
  assert.deepEqual(f.counts, { get: 100, put: 0, alarm: 0 });
  assert.equal(upstream, 0);
});

test('cached requests renew an old idle lease only once per day', async () => {
  const now = Date.now();
  const f = fixture({ accessToken: 'cached', refreshToken: 'refresh',
    scope: 'openid sdk.social_layer_presence', expiresAt: now + 7 * 86400000,
    cleanupAt: now + 29 * 86400000 - 1000 });
  globalThis.fetch = async () => { throw new Error('unexpected network call'); };
  for (let i = 0; i < 100; i++) {
    assert.equal((await f.session.fetch(request())).status, 200);
  }
  assert.deepEqual(f.counts, { get: 100, put: 1, alarm: 1 });
  assert.ok(f.values.get('session').cleanupAt >= now + 30 * 86400000);
  await f.session.alarm();
  assert.equal(f.values.get('session').accessToken, 'cached');
});
