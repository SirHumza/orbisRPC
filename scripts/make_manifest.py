#!/usr/bin/env python3
"""make_manifest.py - build + sign the release manifest for the updater.

Usage:
  python3 scripts/make_manifest.py --version 1.0.1 \
      --priv release_priv.pem \
      --bin build/orbisrpc.elf \
      --prx plugin/build/orbisrpc_plugin.prx \
      --pkg build/pkg/OrbisRPC-1.0.1.pkg \
      --out build/pkg/manifest.json

Outputs manifest.json + manifest.sig (raw 64-byte r||s, hex file) +
appends SHA256SUMS for back-compat. The daemon verifies manifest.sig
with orbisrpc/release_pubkey.h and refuses updates on mismatch
(ORX-UPDATE-002). Keep release_priv.pem OFFLINE, out of the repo.
"""
import argparse, hashlib, json, os, sys

try:
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature, encode_dss_signature
    HAVE_CRYPTO = True
except ImportError:
    HAVE_CRYPTO = False


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True)
    ap.add_argument("--channel", default="stable")
    ap.add_argument("--min-version", default="")
    ap.add_argument("--platform", default="ps4-goldhen")
    ap.add_argument("--priv", required=True, help="EC P-256 private key PEM (offline)")
    ap.add_argument("--bin", default="")
    ap.add_argument("--prx", default="")
    ap.add_argument("--pkg", default="")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    if not HAVE_CRYPTO:
        print("need python cryptography package: pip install cryptography", file=sys.stderr)
        return 1
    assets = []
    for name, path in (("orbisrpc.bin", a.bin), ("orbisrpc_plugin.prx", a.prx)):
        if path and os.path.isfile(path):
            assets.append({"name": name, "sha256": sha256_file(path)})
    pkg_name = ""
    if a.pkg and os.path.isfile(a.pkg):
        pkg_name = os.path.basename(a.pkg)
        assets.append({"name": pkg_name, "sha256": sha256_file(a.pkg)})
    if not assets:
        print("no assets found (bin/prx/pkg missing)", file=sys.stderr)
        return 1
    manifest = {
        "version": a.version,
        "channel": a.channel,
        "platform": a.platform,
        "assets": assets,
    }
    if a.min_version:
        manifest["minimum_version"] = a.min_version
    body = json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()
    priv = serialization.load_pem_private_key(open(a.priv, "rb").read(), password=None)
    if not isinstance(priv, ec.EllipticCurvePrivateKey):
        print("private key is not EC", file=sys.stderr)
        return 1
    der = priv.sign(body, ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(der)
    raw = r.to_bytes(32, "big") + s.to_bytes(32, "big")
    with open(a.out, "wb") as f:
        f.write(body)
    with open(a.out + ".sig", "wb") as f:
        f.write(raw)
    with open(a.out + ".sig.hex", "w") as f:
        f.write(raw.hex())
    # back-compat SHA256SUMS alongside
    sums = os.path.join(os.path.dirname(a.out), "SHA256SUMS")
    with open(sums, "w") as f:
        for ent in assets:
            f.write("%s  %s\n" % (ent["sha256"], ent["name"]))
        f.write("%s  %s\n" % (hashlib.sha256(body).hexdigest(), "manifest.json"))
    print("wrote %s (%d assets) + %s.sig + SHA256SUMS" % (a.out, len(assets), a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
