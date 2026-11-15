#!/usr/bin/env python3
"""Convert a trusted PEM CA bundle to the EFI X.509 signature-list format OVMF reads."""

import argparse
import base64
import hashlib
import re
import struct
import sys
import uuid
from pathlib import Path


X509_GUID = uuid.UUID("a5c059a1-94e4-4aa7-87b5-ab155c2bf072")
OWNER_NAMESPACE = uuid.UUID("b17c2d9f-1acd-4b09-a370-e468e2dfca16")
PEM_CERT = re.compile(
    rb"-----BEGIN CERTIFICATE-----\s*([A-Za-z0-9+/=\s]+?)\s*-----END CERTIFICATE-----"
)


def build_database(pem: bytes) -> tuple[bytes, int]:
    output = bytearray()
    seen: set[bytes] = set()
    for match in PEM_CERT.finditer(pem):
        encoded = re.sub(rb"\s+", b"", match.group(1))
        try:
            certificate = base64.b64decode(encoded, validate=True)
        except ValueError as exc:
            raise ValueError("Invalid base64 certificate in CA bundle") from exc
        if len(certificate) < 64 or certificate[0] != 0x30:
            raise ValueError("Invalid DER certificate in CA bundle")
        digest = hashlib.sha256(certificate).digest()
        if digest in seen:
            continue
        seen.add(digest)
        owner = uuid.uuid5(OWNER_NAMESPACE, digest.hex())
        signature_size = 16 + len(certificate)
        list_size = 28 + signature_size
        output += X509_GUID.bytes_le
        output += struct.pack("<III", list_size, 0, signature_size)
        output += owner.bytes_le
        output += certificate
    if not seen:
        raise ValueError("No PEM CA certificates found")
    return bytes(output), len(seen)


def inspect_database(data: bytes) -> int:
    """Check EDK II signature-list framing and count X.509 entries."""
    offset = count = 0
    while offset < len(data):
        if len(data) - offset < 28:
            raise ValueError("Truncated EFI_SIGNATURE_LIST")
        if data[offset : offset + 16] != X509_GUID.bytes_le:
            raise ValueError("Unsupported signature type in CA database")
        list_size, header_size, signature_size = struct.unpack_from("<III", data, offset + 16)
        if (
            list_size < 28
            or list_size > len(data) - offset
            or header_size != 0
            or signature_size < 17
            or (list_size - 28) % signature_size != 0
        ):
            raise ValueError("Malformed EFI_SIGNATURE_LIST")
        count += (list_size - 28) // signature_size
        offset += list_size
    if not count:
        raise ValueError("Empty CA database")
    return count


def main() -> None:
    if len(sys.argv) == 3 and sys.argv[1] == "--check":
        source = Path(sys.argv[2])
        count = inspect_database(source.read_bytes())
        print(f"Verified {source}: {count} X.509 signatures")
        return
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pem", type=Path, help="trusted PEM CA bundle")
    parser.add_argument("output", type=Path, help="output EDK II CA database")
    args = parser.parse_args()
    data, count = build_database(args.pem.read_bytes())
    if inspect_database(data) != count:
        raise ValueError("Generated CA database failed validation")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temp = args.output.with_suffix(args.output.suffix + ".tmp")
    temp.write_bytes(data)
    temp.replace(args.output)
    print(f"Created {args.output}: {count} certificates, {len(data)} bytes")


if __name__ == "__main__":
    main()
