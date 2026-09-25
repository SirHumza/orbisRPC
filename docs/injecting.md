# Injecting OrbisRPC into the PS4 — the complete guide

This exists because getting a payload to *execute* took longer than
writing the payload. Everything below was learned on a real 9.00 console.

## You need first

- PS4 on 9.00, jailbroken with GoldHEN (2.4b18+ recommended — older
  payloader builds segfault on ELF files, see below).
- Console and computer on the same network. Find the PS4 IP:
  Settings → Network → View Connection Status (ours is `192.168.1.136`).
- The payload file: `build-sdk/orbisrpc_sdk.elf` (built via
  `./scripts/build_sdk.sh`).

## Method 1 — elfldr (recommended)

elfldr is a proper ELF loader that runs payloads as separate processes.

1. Get it listening. Either load `elfldr.elf` through GoldHEN's payloader
   page once, or keep it running — it serves on **port 9021** until reboot.
2. Send the payload with a raw TCP connection, then close the write side:

```python
import socket
data = open("build-sdk/orbisrpc_sdk.elf", "rb").read()
s = socket.socket()
s.settimeout(25)
s.connect(("192.168.1.136", 9021))
s.sendall(data)
try:
    s.shutdown(socket.SHUT_WR)  # signals end-of-payload
except OSError:
    pass
s.close()
```

3. Verify it runs: within ~20 seconds `/data/orbisRPC/log.txt` must contain
   `orbisRPC payload boot`. No file = it never executed.

## Method 2 — GoldHEN BinLoader server (port 9020)

The classic route (NetCat tools, phone apps, scripts all speak it).

1. Arm it: tap **BinLoader** in the exploit host menu (NOT the GoldHEN
   settings payloader page — different loader).
2. Within seconds, fire blind — **one single connection carrying the full
   payload, no port check first**:

```python
import socket
data = open("build-sdk/orbisrpc_sdk.elf", "rb").read()
s = socket.socket()
s.settimeout(25)
s.connect(("192.168.1.136", 9020))
s.sendall(data)
s.close()
```

3. Same verification: look for `orbisRPC payload boot` in the log.

### THE ONE-SHOT RULE (read this twice)

GoldHEN's BinLoader arms **once**. Any empty port check (`connect_ex`,
`nc -z`, port scanners) connects with nothing to send and **burns the
arm** — the real payload then arrives at a dead listener. Sequence is
always: tap → say go → fire immediately → verify. Never probe first.

## Method 3 — in-app injector

The OrbisRPC setup app injects over loopback itself
(`127.0.0.1:9020`, fallback `9090`) from its menu. Needs a listening
loader exactly like Method 2.

## Method 4 — sender page (no PC tools)

`https://SirHumza.github.io/orbisrpc-host/` in the PS4 browser
auto-sends the bundled backend to the console's own loader and prints
the result on screen.

## What NOT to use

- **GoldHEN settings payloader page**: on GoldHEN ≤ 2.4 (pre-b18.5
  lineage) its loader thread segfaults (`SIGSEGV`, null read in
  `GoldHENLoader`) on every ELF — including 90KB hello-worlds. The klog
  prints `payload launched successfully` and then dies. If you see this,
  update GoldHEN, don't rebuild the payload.
- **Raw SELF files**: the page expects ELF (`\x7fELF`). SELF uploads
  report "invalid payload".

## Troubleshooting

| Symptom | Meaning | Fix |
|---|---|---|
| `Connection refused` on 9020/9090 | No listener armed | Tap BinLoader / open payloader page, retry instantly |
| `payload launched successfully` then silence, no log | Loader segfault (see above) | Update GoldHEN ≥ v2.4b18.5, use BinLoader server |
| `Error handling payload` | Loader rejected the bytes | Re-check file integrity (`shasum`), resend |
| Log exists but `FATAL: token rejected (4004)` | Token rotated/dead | Fresh token into `/data/orbisRPC/config.json`, relaunch |
| Multiple `Payload` processes in process list | Old instances piled up | Reboot clears them; the daemon's lock prevents recurrence |

## Watching it work

- Kernel log (loader events, faults): TCP `192.168.1.136:3232`, raw stream.
- Daemon log: `/data/orbisRPC/log.txt` via FTP (`192.168.1.136:2121`,
  anonymous). Absent file = payload never executed, full stop.
- Process list: `sysctl kern.proc` (see `scripts/ps4_watch.py`) — look
  for `Payload` and `eboot.bin` entries.
- Final proof: Discord profile shows the game + ticking timer.
