#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

source_file="${1:-main.mx}"
[[ "$source_file" == *.mx ]] || source_file="${source_file}.mx"
if [[ "$source_file" != *.mx || ! -f "$source_file" ]]; then
    echo "Usage: bash compile.sh [main.mx|examples/wifi.mx]" >&2
    exit 2
fi
command -v python3 >/dev/null || { echo "Missing tool: python3" >&2; exit 2; }
clang_cmd="${MX_CLANG:-clang}"
link_cmd="${MX_LLD_LINK:-lld-link}"
if [[ "$(uname -s)" == Darwin ]] && command -v brew >/dev/null; then
    if [[ -z "${MX_CLANG:-}" ]]; then
        if brew_llvm_prefix="$(brew --prefix llvm 2>/dev/null)"; then
            brew_clang="$brew_llvm_prefix/bin/clang"
            [[ -x "$brew_clang" ]] && clang_cmd="$brew_clang"
        fi
    fi
    if [[ -z "${MX_LLD_LINK:-}" ]]; then
        if brew_lld_prefix="$(brew --prefix lld 2>/dev/null)"; then
            brew_link="$brew_lld_prefix/bin/lld-link"
            [[ -x "$brew_link" ]] && link_cmd="$brew_link"
        fi
    fi
fi
command -v "$clang_cmd" >/dev/null || { echo "Missing Clang: $clang_cmd" >&2; exit 2; }
command -v "$link_cmd" >/dev/null || { echo "Missing LLD linker: $link_cmd" >&2; exit 2; }
name="$(basename "$source_file" .mx)"
mkdir -p build/EFI/BOOT
python3 compiler/mxc.py "$source_file" build/generated.c

target_flags=(-target x86_64-pc-win32 -ffreestanding -fshort-wchar
              -fno-stack-protector -fno-builtin -O2 -Iruntime)
sources=(mx_runtime mx_lowlevel mx_http mx_browser_text mx_browser_layout
         mx_browser_js mxWiFi mxEthernet)
objects=()
for unit in "${sources[@]}"; do
    "$clang_cmd" "${target_flags[@]}" -c "runtime/$unit.c" -o "build/$unit.obj"
    objects+=("build/$unit.obj")
done
"$clang_cmd" "${target_flags[@]}" -c build/generated.c -o build/generated.obj
objects+=(build/generated.obj)
"$link_cmd" /subsystem:efi_application /entry:efi_main /nodefaultlib \
    /out:build/BOOTX64.EFI "${objects[@]}"
cp build/BOOTX64.EFI build/EFI/BOOT/BOOTX64.EFI
python3 scripts/make_uefi_iso.py build "$name"
echo "Ready: build/$name.iso"
