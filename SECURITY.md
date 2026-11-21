# Security

## OAuth beta credentials

OAuth mode keeps the Discord application secret and user refresh tokens in the
Cloudflare broker. The PS4 stores a broker session and its expiring access-token cache in
`oauth-session.json`, written atomically at mode `0600`. Never share that
session file. Cached tokens are origin-bound and checked against issue/expiry
times after restart. Rejection removes the cached token and persists a parked
state until unlinking. Broker URLs
must be trusted HTTPS origins. Unlink through `oauth.unlink` or revoke the
application through Discord. Account access can be revoked by the user or
Discord, so recovery can require fresh approval.

## Discord user tokens

The daemon connects to Discord's gateway as you, using your user session
token. That string grants **full account access**:

- Never share `config.json`. Never paste a token in Discord, logs, or issues.
- Never commit a token. CI scans shipped files for token-shaped strings
  and fails the build (`ORBISRPC_STRICT_SECRETS=1`).
- Token stopped working (close `4004`)? It died on Discord's side
  (password change / logout-all). Paste a fresh one; no reinstall needed.
- Token-based presence violates Discord's ToS. No bans for non-spam
  presence use are known, but the risk is yours.

## Webhook

Commit notifications post via the `DISCORD_WEBHOOK_URL` repo secret
(Settings → Secrets → Actions). The URL itself is a secret: never commit
it, and regenerate it in Discord if it ever leaks.

## Reporting

Found a vulnerability? Report it in the Discord server, not in a public
issue. See the README for the invite.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms. -->
