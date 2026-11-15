param([string]$Name = 'main', [switch]$Https)
$ErrorActionPreference = 'Stop'

Push-Location -LiteralPath $PSScriptRoot
try {
    if ($Name.EndsWith('.mx', [StringComparison]::OrdinalIgnoreCase)) {
        $Name = [IO.Path]::GetFileNameWithoutExtension($Name)
    }
    $iso = Join-Path 'build' "$Name.iso"
    if (-not (Test-Path -LiteralPath $iso -PathType Leaf)) {
        throw "ISO not found: $iso. Run .\compile.ps1 $Name.mx first."
    }

    $qemuCommand = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
    if ($env:MX_QEMU) { $qemuPath = $env:MX_QEMU }
    elseif ($qemuCommand) { $qemuPath = $qemuCommand.Source }
    else { $qemuPath = Join-Path $env:ProgramFiles 'qemu/qemu-system-x86_64.exe' }
    if (-not (Test-Path -LiteralPath $qemuPath -PathType Leaf)) {
        throw 'QEMU not found. Install QEMU or set MX_QEMU to qemu-system-x86_64.exe.'
    }

    $qemuDir = Split-Path -Parent $qemuPath
    if ($env:MX_OVMF_CODE) { $ovmf = $env:MX_OVMF_CODE }
    else { $ovmf = Join-Path $qemuDir 'share/qemu/edk2-x86_64-code.fd' }
    if (-not (Test-Path -LiteralPath $ovmf -PathType Leaf)) {
        throw 'OVMF image not found. Set MX_OVMF_CODE to the x86-64 firmware image.'
    }

    $tlsArgs = @()
    if ($env:MX_TLS_CERTS) { $certs = $env:MX_TLS_CERTS }
    elseif (Test-Path -LiteralPath 'build/cacerts-certifi.bin' -PathType Leaf) { $certs = 'build/cacerts-certifi.bin' }
    else { $certs = '' }
    if ($certs) {
        if (-not (Test-Path -LiteralPath $certs -PathType Leaf)) { throw "CA database not found: $certs" }
        $python = Get-Command python -ErrorAction SilentlyContinue
        if (-not $python) { throw 'Python 3 is required to validate the CA database.' }
        & $python.Source 'scripts/make_ca_db.py' '--check' $certs
        if ($LASTEXITCODE -ne 0) { throw 'Invalid CA database.' }
        $tlsArgs = @('-fw_cfg', "name=etc/edk2/https/cacerts,file=$certs")
    }
    elseif ($Https) { throw 'HTTPS requires an edk2 CA database. Set MX_TLS_CERTS.' }

    $qemuArgs = @('-m', '1024', '-machine', 'pc', '-vga', 'std',
                  '-drive', "if=pflash,format=raw,readonly=on,file=$ovmf",
                  '-device', 'qemu-xhci,id=xhci', '-device', 'usb-tablet,bus=xhci.0',
                  '-device', 'virtio-rng-pci', '-nic', 'user,model=e1000')
    $qemuArgs += $tlsArgs
    $qemuArgs += @('-cdrom', $iso, '-boot', 'd')
    & $qemuPath @qemuArgs
    if ($LASTEXITCODE -ne 0) { throw "QEMU exited with code $LASTEXITCODE" }
}
finally {
    Pop-Location
}
