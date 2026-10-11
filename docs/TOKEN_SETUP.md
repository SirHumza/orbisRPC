# Getting your Discord token (2 minutes)

The daemon needs your Discord **user session token**. It goes in one
place only: `/data/orbisRPC/config.json`, in the `"token"` slot.

## Step 1 — copy it from your browser

1. On your PC/Mac, open Discord in a browser (Thorium, Chrome, anything
   logged in) and press **F12** (DevTools).
2. At the top of the console panel, make sure the context dropdown says
   **`top`** — not a worker. (Wrong context gives
   `localStorage is not defined`.)
3. Paste this and hit Enter:
   ```
   copy(localStorage.token)
   ```
   The token is now on your clipboard. It looks like
   `MTM4...Gs0Z...-Tcr8...` (three parts separated by dots).

Do NOT paste the token into any Discord chat or server — Discord
auto-invalidates leaked tokens on sight, and then it stops working.

## Step 2 — paste it into config.json

1. Open `/data/orbisRPC/config.json` on the PS4 (FTP it to your PC,
   edit, send it back — or edit in place).
2. Replace `"SET_ME"` with your token, keeping the quotes:
   ```json
   {"token": "paste-it-here", ...}
   ```
3. Save, re-run the payload from `/data/payloads`.

## Step 3 — check it worked

- Launch a game → your Discord profile shows it within ~30 seconds.
- In `log.txt`: `discord: gateway ready`, no `4004` lines.
- If you see `gateway closed during auth: 4004`, the token is dead —
  redo Step 1 (password changes and "log out everywhere" kill tokens).

One account per token. Each login mints a fresh one; old copies die.
