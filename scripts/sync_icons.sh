#!/bin/bash
# sync_icons.sh - pull /user/appmeta/<TITLEID>/icon0.png off the PS4 into
# config/icons/<lower titleid>.png, ready to host as the art pack.
# Usage: ./scripts/sync_icons.sh [PS4_IP]   default: 192.168.1.136
# Nothing here touches the console beyond FTP reads.
set -euo pipefail
IP="${1:-192.168.1.136}"
OUT="config/icons"
mkdir -p "$OUT"
python3 - "$IP" "$OUT" <<'EOF'
import sys, os
from ftplib import FTP
ip, out = sys.argv[1], sys.argv[2]
f = FTP()
f.connect(ip, 2121, timeout=15)
f.login()
names = f.nlst('/user/appmeta')
got, miss = 0, []
for t in names:
    if len(t) != 9:
        continue
    dst = os.path.join(out, t.lower() + '.png')
    if os.path.exists(dst):
        got += 1
        continue
    try:
        with open(dst, 'wb') as fh:
            f.retrbinary('RETR /user/appmeta/%s/icon0.png' % t, fh.write)
        # validate PNG magic
        with open(dst, 'rb') as fh:
            if fh.read(8) != b'\x89PNG\r\n\x1a\n':
                os.remove(dst)
                miss.append(t)
                continue
        got += 1
    except Exception:
        miss.append(t)
        try: os.remove(dst)
        except OSError: pass
print('icons: %d ok, %d missing (%s)' % (got, len(miss), ','.join(miss[:10])))
EOF
