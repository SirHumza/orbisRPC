# orbisRPC 1.2 beta implementation plan

Use a Cloudflare Worker to broker Discord OAuth device authorization. The PS4
continues to detect the foreground application and publishes activity over its
own TLS Gateway connection. Pairing requires a browser once. Routine play
requires the console, Discord, and the broker at token renewal.

## Work and acceptance

1. Implement device authorization, bounded polling, cached access tokens,
   serialized refresh, and unlink in a Worker. Store the Discord application
   secret as a Worker secret. Keep refresh tokens in durable storage. Verify
   pending, denied, expired, rate-limited, refresh, and concurrent requests.
2. Add explicit OAuth mode to the PS4 daemon. Save its broker session atomically
   at mode 0600, display the public approval code, respect polling intervals,
   reconnect before access-token expiry, and recover after rejection. Preserve
   the existing user-token configuration for upgrades.
3. Add the OAuth Gateway identification path, separate from user-token
   identification. Keep RESUME and heartbeat behavior. Verify the generated
   identification payload and secret redaction on the host.
4. Keep automatic foreground detection independent of authentication. Issue #49
   needs a console trace for the supplied homebrew/media title IDs. Missing
   artwork or a title name must retain the raw title-ID fallback. Track title
   reports in #37. Existing detection regressions run with the host suite.
5. Run the repository host, sanitizer, and strict end-to-end checks. Build the
   payload with daemon-world linkage. Record failures and outstanding evidence.
6. Deploy the broker with the project's own Discord app credentials. Confirm
   that the requested Social SDK presence scope is granted. On the 9.00 Slim,
   approve pairing, switch games, interrupt the network, restart, suspend and
   resume, unlink, and relink. Run beyond token renewal and overnight while
   checking the original desktop login independently. These are release gates.

## Verification record

The local branch is `beta`, with payload version `1.2.0-beta`. Host utility,
WebSocket and network suites pass, including OAuth pairing, polling, token
acceptance, rejected-token parking and unlink fixtures. The address/undefined
sanitizer run passes. `ORBISRPC_STRICT_SECRETS=1 python3
tests/e2e_consumer.py` reports `E2E host simulation: ALL PASS`. The Worker has 14 passing mocked tests and a successful
Wrangler deployment dry run.

The SDK payload compiled and linked as `build-sdk/orbisrpc_sdk.elf` (2,275,640
bytes). Its dynamic dependencies are `libkernel_web.sprx`,
`libSceLibcInternal.sprx`, `libSceNet.sprx`, and `libSceSystemService.sprx`.
The installed SDK's original CRT source is unavailable here, so the source
firmware gate for 13.52 could not be checked. A successful build proves linkage,
while firmware boot compatibility remains a console check.

Cloudflare deployment completed on 2026-11-21 at
`https://orbisrpc-auth.remyx0000.workers.dev`. Its live pairing endpoint returns
HTTP 503 `not_configured`, as expected while Discord application secrets are
absent. Wrangler reports the Durable Object and rate limiter provisioned.
Discord application credentials, granted-scope acceptance, actual Gateway
publication, overnight stability and original-client login retention remain
outstanding. Cloudflare credentials were found in Wrangler's macOS credential
store through `wrangler whoami`.

## Broker cost optimization

A measured 100-request cached-token batch changed from 100 record writes and
100 alarm writes to zero record/alarm writes, while preserving 100 reads and
zero upstream requests. Idle leases extend at most daily and can expire
29–30 days after the last token request. The PS4 persists the access token
and validity dates alongside its broker session at mode `0600`, allowing
valid-cache restarts to make zero broker requests. A rejected token is removed
from the cache and its rejected state persists until unlinking.

Cloudflare version `3703e24c-cb92-4a38-80c8-ca0e761ecbab` includes the write
coalescing optimization. The live endpoint still requires Discord application
credentials before pairing. Capacity estimates and their assumptions are in
[the broker README](../backend/README.md#free-tier-capacity).

## Issue coverage

Issue #55 motivates app-scoped authorization. Host checks establish protocol
handling. Only a real Discord session can establish whether this fixes the
reported desktop logout. Issue #56 remains the legacy Gateway research record.
App authorization can be revoked and network services can fail. Recovery must
handle these conditions and request pairing when authorization ends.

## Sources and attribution

Tevtongermany [ONET] and [OrbisNet](https://github.com/OrbisNet) supplied the
architecture explanation and a private C++ reference. Credit their assistance
in derived behavior. Their private file stays outside this repository.
[Kizzy](https://github.com/dead8309/Kizzy) is an additional protocol reference.
Its login uses a full user session token, while OrbisNet's OAuth branch obtains
an expiring bearer token from its backend.

Discord documents a manual console device flow in its
[console account-linking guide](https://github.com/discord/discord-api-docs/blob/main/developers/discord-social-sdk/development-guides/account-linking-on-consoles.mdx).
The broker implements the documented HTTP flow. The PS4 OAuth Gateway path is
an interoperability implementation informed by the supplied reference. Its
server acceptance requires live verification with our own Discord application.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms. -->
