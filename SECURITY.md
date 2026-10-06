# Security

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
