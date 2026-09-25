#!/usr/bin/env python3
"""make_ca_bundle.py - curate a minimal CA bundle for the PS4 TLS client.

Extracts the roots needed for GitHub (Sectigo/DigiCert), Discord
(Google Trust Services), and Sony TMDB (DigiCert/Amazon/ISRG) from the
local OpenSSL bundle, writes orbisrpc/ca_bundle_pem.h as a C string.

Usage: python3 scripts/make_ca_bundle.py [--system PATH] [--out PATH]
Re-run when a handshake fails with ORX-TLS-003 due to root rotation.
"""
import argparse, os, re, subprocess, sys, tempfile

WANT = [
    "Sectigo Public Server Authentication Root E46",
    "Sectigo Public Server Authentication Root R46",
    "USERTrust ECC Certification Authority",
    "USERTrust RSA Certification Authority",
    "DigiCert Global Root CA",
    "DigiCert Global Root G2",
    "DigiCert Global Root G3",
    "DigiCert High Assurance EV Root CA",
    "DigiCert Trusted Root G4",
    "GTS Root R1",
    "GTS Root R2",
    "GTS Root R3",
    "GTS Root R4",
    "ISRG Root X1",
    "Amazon Root CA 1",
    "Baltimore CyberTrust Root",
    "Starfield Services Root Certificate Authority - G2",
    "GlobalSign Root CA",
]


def split_pem(path):
    data = open(path).read()
    return re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", data, re.S)


def subject_of(pem):
    with tempfile.NamedTemporaryFile("w", delete=False, suffix=".pem") as f:
        f.write(pem)
        fn = f.name
    try:
        out = subprocess.run(["openssl", "x509", "-noout", "-subject", "-in", fn],
                             capture_output=True, text=True).stdout.strip()
    finally:
        os.unlink(fn)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--system", default="/usr/local/etc/openssl@3/cert.pem")
    ap.add_argument("--out", default="orbisrpc/ca_bundle_pem.h")
    a = ap.parse_args()
    if not os.path.isfile(a.system):
        a.system = "/etc/ssl/cert.pem"
    blocks = split_pem(a.system)
    picked = []
    for b in blocks:
        subj = subject_of(b)
        for w in WANT:
            if w.lower() in subj.lower():
                picked.append(b)
                break
    if len(picked) < 8:
        print("too few roots matched (%d); refusing" % len(picked), file=sys.stderr)
        return 1
    c = "/* ca_bundle_pem.h - curated trust anchors (generated, do not edit).\n"
    c += " * %d roots for GitHub/Discord/Sony. Regenerate with scripts/make_ca_bundle.py.\n" % len(picked)
    c += " */\n#ifndef ORBISRPC_CA_BUNDLE_PEM_H\n#define ORBISRPC_CA_BUNDLE_PEM_H\n"
    c += "static const char ORBISRPC_CA_BUNDLE_PEM[] =\n"
    bundle = "\n".join(picked) + "\n"
    for line in bundle.splitlines(True):
        c += '"%s\\n"\n' % line.rstrip("\n").replace('"', "'")
    c += ";\n#endif\n"
    open(a.out, "w").write(c)
    print("wrote %s (%d roots, %d bytes)" % (a.out, len(picked), len(bundle)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
