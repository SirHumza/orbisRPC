# orbisRPC OAuth broker

This Worker keeps Discord's client secret and rotating refresh token off the PS4. Each console receives a random 256-bit broker session and a short-lived Discord bearer token. The console owns its Discord WebSocket connection. Tevtongermany [ONET] and [OrbisNet](https://github.com/OrbisNet) supplied the architectural reference. This implementation uses public Discord OAuth documentation and does not redistribute their private source.

## Deploy

Create an orbisRPC Discord application and enable Social SDK access. The application must be permitted to request `openid sdk.social_layer_presence`. Acceptance of that scope and bearer Gateway activity publishing must be verified with the application before treating this as working Discord integration. Hosting this broker alone grants no Discord permissions.

Run `npm install`, then `npx wrangler login`. Set `DISCORD_CLIENT_ID` and `DISCORD_CLIENT_SECRET` with `npx wrangler secret put NAME` for each name, then run `npm run deploy`. Wrangler provisions the SQLite Durable Object namespace and migrations. Set the resulting HTTPS Worker origin as the PS4's broker URL. No user session token or application secret belongs in the PS4 config. Free-plan request and Durable Object quotas apply.

## Current deployment

The beta broker is deployed at `https://orbisrpc-auth.remyx0000.workers.dev`.
Cloudflare version `3703e24c-cb92-4a38-80c8-ca0e761ecbab` was deployed on
2026-11-21. The live pairing endpoint returns HTTP 503 with `not_configured`
until `DISCORD_CLIENT_ID` and `DISCORD_CLIENT_SECRET` are provisioned.
Cloudflare accepted the Durable Object migration and rate-limit binding.

## Free-tier capacity

Game detection and Discord activity updates stay on each PS4. The console now
persists its expiring access token at mode `0600`, so restarting the payload
reuses valid authorization. The broker receives pairing polls and token
renewal requests. Actual token lifetimes come from Discord's token response.

Workers Free provides 100,000 incoming requests/day. SQLite Durable Objects
have separate limits of 100,000 requests/day, 100,000 rows written/day,
5 million rows read/day, 5 GB stored, and 13,000 GB-s duration/day.

Measured in the mocked storage fixture, 100 cached-token requests previously
caused 100 record writes and 100 alarm writes. They now cause zero of either,
100 reads, and zero upstream Discord requests. Once an idle lease needs its
daily extension, a batch of 100 cached requests causes just one record write
and one alarm write. The console restart fixture previously made one broker
request after restart. A restart with valid persisted authorization now makes
zero requests. These counts are regression assertions in the test suites.

With a seven-day token lifetime, an illustrative steady population of 100,000
regular clients would average about 14,300 renewals/day, independent of daily
restart counts. A renewal writes a polling guard, the rotated token record,
and its alarm, approximately 42,900 writes/day across that population. This
assumes renewal traffic is spread across the week and excludes pairing,
failed requests, unlinking, lease extensions and other account workloads.
All clients renewing together can exceed daily quotas. A thousand new links
taking about a minute each at five-second polling adds roughly 14,000 requests
and 16,000 writes. Shorter token lifetimes increase renewal traffic.

These are workload estimates based on operation counts, not a live throughput
or latency benchmark. Discord's limits, CPU duration, storage and shared
account quotas also apply. At limits, free-tier operations fail until reset.

Sources are [Worker limits](https://developers.cloudflare.com/workers/platform/limits/)
and [Durable Object pricing](https://developers.cloudflare.com/durable-objects/platform/pricing/).

## HTTP contract

All responses disable caching. Only HTTPS should be exposed. Errors are `{ "error": { "code": "...", "message": "..." } }`. Bodies are never logged.

* `POST /v1/pair` starts a fresh Discord device grant. `201` contains `session_token`, `user_code`, `verification_uri`, `verification_uri_complete`, `expires_in`, and `interval`. Persist the broker session privately. Pairing is limited to five starts per IP per minute. Lost responses can leave an unclaimed device grant until expiry. This operation is not idempotent.
* `POST /v1/token` requires `Authorization: Bearer SESSION_TOKEN`. `200` contains `access_token`, `token_type` (`Bearer`), `expires_in`, and `scope`. Prepend `Bearer ` only for Discord authentication. `202` contains `{ "status": "authorization_pending", "interval": N }`. Wait that many seconds. `429` supplies `Retry-After`. `401` requires pairing again. Other failures need backoff. Refresh exchanges run in a serialized Durable Object queue.
* `DELETE /v1/session` requires the same broker authorization. `204` removes the session and revokes the Discord grant if issued. Repeating deletion returns `401`. If Discord revocation fails, token retrieval is blocked, a tombstone retains credentials for retry, and `502` or `503` instructs the user to retry unlinking or revoke the application in Discord settings.

Pending grants expire with Discord's device code. Active records expire approximately 29–30 days after the last token retrieval. Daily lease coalescing can bring cleanup forward by up to one day. Idle expiry does not revoke Discord authorization. Users can remove it in Discord's Authorized Apps settings. Access and refresh tokens rotate together. A process/storage failure after Discord rotates them but before persistence can require pairing again. Revocation, server outages, quotas, and denied authorization remain supported error states.

## Verification

Run `npm test` for mocked grant, refresh, concurrency, scope validation, and revocation tests. These do not prove live application permissions, PS4 connection, or desktop session stability.

Sources are [Discord console device authorization](https://github.com/discord/discord-api-docs/blob/main/developers/discord-social-sdk/development-guides/account-linking-on-consoles.mdx) and [Cloudflare Durable Objects](https://developers.cloudflare.com/durable-objects/).

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms. -->
