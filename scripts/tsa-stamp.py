#!/usr/bin/env python3
"""Add RFC 3161 TSA stamps to all signature nodes in a signed FIT image.
Each tsa-stamp is inserted immediately after the signature node's value property.

Usage:
  ./tsa-stamp.py image.fit http://localhost:3161
  ./tsa-stamp.py image.fit freetsa --ca-bundle tsa-keys/freetsa-bundle.pem
  ./tsa-stamp.py image.fit digitcert
  ./tsa-stamp.py image.fit sectigo
  ./tsa-stamp.py image.fit globalsign
"""

import argparse
import struct
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path
from urllib.parse import urlparse

# Known public RFC 3161 TSA aliases -> TSP endpoint URL.
# Any other string passed as tsa_url is used verbatim (with http:// prepended
# if no scheme is given).
TSA_ALIASES = {
    "freetsa":    "https://freetsa.org/tsr",
    "digicert":   "http://timestamp.digicert.com",
    "sectigo":    "http://timestamp.sectigo.com",
    # GlobalSign's RFC 3161 responder as pointed to by TBS Certificates'
    # timestamping FAQ (https://www.tbs-certificates.co.uk/FAQ/en/globalsign_cs_timestamp.html);
    # confirmed live and RFC 3161 (405 on a bare GET, matching the POST-only
    # TSP protocol) — not to be confused with GlobalSign's legacy Authenticode
    # endpoint at scripts/timstamp.dll, which is a different, non-RFC3161 wire format.
    "globalsign": "http://pki.codegic.com/codegic-service/timestamp",
    "codegic":    "http://pki.codegic.com/codegic-service/timestamp",
}


def _run(*args):
    subprocess.run(args, check=True, stderr=subprocess.DEVNULL)


def _fdt_header(dtb):
    magic, totalsize, off_struct, off_strings, _rsvmap, _ver, _lcomp, _cpu, size_strings, size_struct = \
        struct.unpack_from(">10I", dtb)
    assert magic == 0xd00dfeed, "not a valid FDT"
    return totalsize, off_struct, off_strings, size_strings, size_struct


def _fdt_find_value_nodes(dtb):
    """Walk the FDT; return [(node_path, value_bytes, insert_at), ...] in traversal order."""
    _, off_struct, off_strings, _, size_struct = _fdt_header(dtb)

    results, path, stack = [], [], []
    pos = off_struct

    while pos < off_struct + size_struct:
        token = struct.unpack_from(">I", dtb, pos)[0]
        pos += 4

        if token == 1:   # FDT_BEGIN_NODE
            end = dtb.index(0, pos)
            path.append(dtb[pos:end].decode())
            pos = (end + 4) & ~3
            stack.append(None)

        elif token == 2: # FDT_END_NODE
            state = stack.pop()
            if state:
                results.append(("/".join(path), *state))
            path.pop()

        elif token == 3: # FDT_PROP
            plen, nameoff = struct.unpack_from(">II", dtb, pos)
            data = bytes(dtb[pos + 8: pos + 8 + plen])
            pos += 8 + ((plen + 3) & ~3)
            ns = off_strings + nameoff
            name = dtb[ns:dtb.index(0, ns)].decode()
            if name == "value" and stack:
                stack[-1] = (data, pos)  # (value_bytes, offset right after value record)

        elif token == 9: # FDT_END
            break

    return results  # [(node_path, value_bytes, insert_at), ...]


def _fdt_insert_prop(dtb, insert_at, prop_name, prop_data):
    """Return a new bytearray with prop_name=prop_data inserted at insert_at."""
    totalsize, off_struct, off_strings, size_strings, size_struct = _fdt_header(dtb)

    new_nameoff = size_strings
    name_b = prop_name.encode() + b"\x00"
    pad = (-len(prop_data)) % 4
    prop_rec = struct.pack(">III", 3, len(prop_data), new_nameoff) + prop_data + b"\x00" * pad

    new_dtb = bytearray(
        dtb[:insert_at]
        + prop_rec
        + dtb[insert_at:off_strings]
        + dtb[off_strings:off_strings + size_strings]
        + name_b
        + dtb[off_strings + size_strings:totalsize]
    )

    new_off_strings = off_strings + len(prop_rec)
    struct.pack_into(">I", new_dtb, 4,  totalsize + len(prop_rec) + len(name_b))
    struct.pack_into(">I", new_dtb, 12, new_off_strings)
    struct.pack_into(">I", new_dtb, 32, size_strings + len(name_b))
    struct.pack_into(">I", new_dtb, 36, size_struct   + len(prop_rec))

    return new_dtb


def _extract_tsa_cert(resp_f, tmp, pem_out):
    """Pull the TSA's own signing certificate out of a timestamp response and
    save it as PEM at pem_out, for embedding in the bootloader's trust store
    (CONFIG_CRYPTO_PUBLIC_KEYS keyring=tsa,fit-hint=<policy OID>:pem_out).

    Requires the query to have been made with certReq=true (the -cert flag
    below): only then is the TSA required by RFC 3161 to embed its signing
    cert in the response's CMS SignedData.certificates field.
    """
    token_f = f"{tmp}/token.der"
    steps = [
        (["openssl", "ts", "-reply", "-in", resp_f,
          "-out", token_f, "-token_out"], "extracting TimeStampToken from response"),
        (["openssl", "pkcs7", "-inform", "DER", "-in", token_f,
          "-print_certs", "-out", str(pem_out)], "extracting certificates from token"),
    ]
    for cmd, desc in steps:
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"[!] {desc} failed:\n{r.stderr.strip()}", file=sys.stderr)
            return None

    if not Path(pem_out).stat().st_size:
        return None

    subject = subprocess.run(
        ["openssl", "x509", "-in", str(pem_out), "-noout", "-subject"],
        capture_output=True, text=True).stdout.strip()
    return subject or "(certificate extracted, subject unreadable)"


def _request_token(sig_bytes, url, ca, tmp, pem_out):
    sig_f, req_f, resp_f = f"{tmp}/sig.bin", f"{tmp}/ts.req", f"{tmp}/ts.resp"
    Path(sig_f).write_bytes(sig_bytes)
    _run("openssl", "ts", "-query", "-data", sig_f, "-sha256", "-cert", "-no_nonce", "-out", req_f)

    req = urllib.request.Request(
        url, data=Path(req_f).read_bytes(),
        headers={"Content-Type": "application/timestamp-query"})
    with urllib.request.urlopen(req) as r:
        token = r.read()
    Path(resp_f).write_bytes(token)

    if ca:
        _run("openssl", "ts", "-verify", "-queryfile", req_f, "-in", resp_f, "-CAfile", ca)

    r = subprocess.run(["openssl", "ts", "-reply", "-in", resp_f, "-text"],
                        capture_output=True, text=True)
    policy_oid = None
    for line in r.stdout.splitlines():
        if "polic" in line.lower():
            print(f"    {line.strip()}")
            policy_oid = line.split(":", 1)[-1].strip()

    if pem_out is not None and not pem_out.exists():
        subject = _extract_tsa_cert(resp_f, tmp, pem_out)
        if subject:
            print(f"[*] TSA certificate extracted → {pem_out}  ({subject})")
        else:
            print(f"[!] TSA did not embed its certificate in the response "
                  f"(no certReq support?) — {pem_out} not written", file=sys.stderr)

    return token, policy_oid


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image",   help="FIT image (modified in place)")
    ap.add_argument("tsa_url", help="TSA server URL or one of: "
                     + ", ".join(sorted(TSA_ALIASES)))
    ap.add_argument("--ca-bundle", help="CA bundle PEM for token verification")
    ap.add_argument("--pem-out", help="where to save the TSA's signing certificate "
                     "(PEM) for embedding in the bootloader's trust store "
                     "(default: <tsa-host>.pem in the current directory)")
    args = ap.parse_args()

    url = TSA_ALIASES.get(args.tsa_url.lower(), args.tsa_url)
    if not url.startswith("http"):
        url = f"http://{url}"

    pem_out = Path(args.pem_out) if args.pem_out else Path(f"{urlparse(url).hostname}.pem")

    with tempfile.TemporaryDirectory() as tmp:
        ca = args.ca_bundle
        if ca is None:
            try:
                with urllib.request.urlopen(f"{url}/cert.pem") as r:
                    ca = f"{tmp}/tsa.pem"
                    Path(ca).write_bytes(r.read())
            except Exception:
                print("[!] cannot fetch TSA cert — skipping verification", file=sys.stderr)

        dtb = bytearray(Path(args.image).read_bytes())
        # Only stamp configuration signature nodes — image hash nodes (/images/*/hash-*)
        # are listed in hashed-nodes and covered by the signature; modifying them
        # would corrupt the signature.
        nodes = [n for n in _fdt_find_value_nodes(dtb)
                 if n[0].lstrip("/").startswith("configurations/")]

        if not nodes:
            sys.exit("no signature nodes with 'value' found")

        print(f"[*] {len(nodes)} signature node(s) found")

        # fetch all tokens first (insert_at values are from the original, unpatched FDT)
        pending = []
        policy_oid = None
        for node_path, value_bytes, insert_at in nodes:
            print(f"[*] {node_path}: requesting token for {len(value_bytes)}-byte signature")
            token, policy_oid = _request_token(value_bytes, url, ca, tmp, pem_out)
            print(f"    {len(token)} bytes {'[verified]' if ca else '[unverified]'}")
            pending.append((insert_at, node_path, token))

        # patch in reverse offset order — later inserts don't shift earlier insert_at values
        for insert_at, node_path, token in sorted(pending, key=lambda x: x[0], reverse=True):
            dtb = _fdt_insert_prop(dtb, insert_at, "tsa-token", token)
            print(f"[*] tsa-stamp inserted after value → {node_path}/tsa-token")

        Path(args.image).write_bytes(dtb)

        if pem_out.exists():
            hint = policy_oid or "<TSTInfo.policy OID>"
            print(f"[*] Embed for verification: keyring=tsa,fit-hint={hint}:{pem_out}")
        else:
            print(f"[!] No TSA certificate was extracted this run — "
                  f"{pem_out} not written, nothing to embed", file=sys.stderr)


if __name__ == "__main__":
    main()
