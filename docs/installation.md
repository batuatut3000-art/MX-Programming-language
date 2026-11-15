# Installation and build

All three host systems build the same x86-64 UEFI program. Run commands from the `MX` directory. The output is `build/main.iso` and `build/EFI/BOOT/BOOTX64.EFI`.

## Linux and WSL

Install Python 3, Clang, LLD (`lld-link`), and Bash. On Ubuntu/WSL:

```bash
sudo apt update
sudo apt install python3 clang lld qemu-system-x86 ovmf
bash compile.sh
bash run-qemu.sh main
```

`bash compile.sh examples/wifi.mx` produces `build/wifi.iso`. `bash compile.sh main` also works.

## Windows

Install Python 3, LLVM (Clang and `lld-link`), and optionally QEMU. Put Python and LLVM's `bin` directory on `PATH`. In PowerShell:

```powershell
.\compile.ps1 main.mx
.\run-qemu.ps1 main
```

`run-qemu.ps1` looks for QEMU on `PATH` or under `C:\Program Files\qemu`. If needed, set `$env:MX_QEMU` to the QEMU executable and `$env:MX_OVMF_CODE` to an x86-64 OVMF code image. For HTTPS, set `$env:MX_TLS_CERTS` to an EDK II CA database and run `.\run-qemu.ps1 main -Https`.

If local PowerShell policy blocks the scripts, start one process with `powershell -NoProfile -ExecutionPolicy RemoteSigned -File .\compile.ps1 main.mx`.

## macOS

Install Homebrew packages:

```bash
brew install python llvm lld qemu
bash compile.sh main.mx
bash run-qemu.sh main
```

`compile.sh` finds Homebrew's Clang and `lld-link`; `run-qemu.sh` looks for QEMU's x86-64 EDK II code image. On Apple Silicon this x86-64 guest runs under QEMU emulation. Set `MX_CLANG`, `MX_LLD_LINK`, or `MX_OVMF_CODE` if your tools are elsewhere.

For HTTPS in QEMU, use `bash run-qemu.sh main --https`. The launcher prepares a firmware CA database from the system trust store when possible. HTTPS still depends on the selected OVMF image and TLS compatibility with the server.

Run `bash scripts/test.sh` for native checks on Linux or macOS. Build the VS Code extension with `python3 vscode-mxpl/package_vsix.py` and install the resulting VSIX through VS Code's **Extensions: Install from VSIX...** command.
