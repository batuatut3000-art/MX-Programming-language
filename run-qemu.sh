#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
name="${1:-main}"
iso="build/${name%.mx}.iso"
ovmf="${MX_OVMF_CODE:-}"
if [[ -z "$ovmf" && -f /usr/share/OVMF/OVMF_CODE_4M.fd ]]; then
    ovmf=/usr/share/OVMF/OVMF_CODE_4M.fd
fi
if [[ -z "$ovmf" ]] && command -v brew >/dev/null; then
    if brew_qemu_prefix="$(brew --prefix qemu 2>/dev/null)"; then
        brew_ovmf="$brew_qemu_prefix/share/qemu/edk2-x86_64-code.fd"
        [[ -f "$brew_ovmf" ]] && ovmf="$brew_ovmf"
    fi
fi
require_https=0
if [[ "${2:-}" == "--https" ]]; then require_https=1; fi
if [[ ! -f "$iso" ]]; then
    echo "ISO not found: $iso (run: bash compile.sh ${name%.mx}.mx)" >&2
    exit 2
fi
if [[ ! -f "$ovmf" ]]; then
    echo "OVMF not found. Set MX_OVMF_CODE to an x86-64 OVMF code image." >&2
    exit 2
fi
tls_args=()
certs="${MX_TLS_CERTS:-}"
if [[ -z "$certs" && -f /etc/pki/ca-trust/extracted/edk2/cacerts.bin ]]; then
    certs=/etc/pki/ca-trust/extracted/edk2/cacerts.bin
fi
if [[ -z "$certs" ]] && command -v p11-kit >/dev/null 2>&1; then
    if p11-kit extract --format=edk2-cacerts --filter=ca-anchors \
        --overwrite --purpose=server-auth build/cacerts.bin; then
        certs=build/cacerts.bin
    fi
fi
if [[ -z "$certs" && -r /etc/ssl/certs/ca-certificates.crt ]]; then
    if python3 scripts/make_ca_db.py /etc/ssl/certs/ca-certificates.crt build/cacerts.bin; then
        certs=build/cacerts.bin
    fi
fi
if [[ -z "$certs" && -s build/cacerts-certifi.bin ]]; then
    certs=build/cacerts-certifi.bin
fi
if [[ -n "$certs" ]]; then
    if [[ ! -s "$certs" ]]; then
        echo "TLS CA database not found or empty: $certs" >&2
        exit 2
    fi
    python3 scripts/make_ca_db.py --check "$certs"
    echo "OVMF HTTPS CA database: $certs"
    tls_args=(-fw_cfg "name=etc/edk2/https/cacerts,file=$certs")
else
    echo "HTTPS needs OVMF TLS drivers and a CA database; set MX_TLS_CERTS to an edk2-cacerts file or install p11-kit." >&2
    if [[ "$require_https" == 1 ]]; then exit 2; fi
fi
qemu-system-x86_64 -m 1024 -machine pc -vga std \
    -drive "if=pflash,format=raw,readonly=on,file=$ovmf" \
    -device qemu-xhci,id=xhci -device usb-tablet,bus=xhci.0 \
    -device virtio-rng-pci \
    -nic user,model=e1000 \
    "${tls_args[@]}" \
    -cdrom "$iso" -boot d
