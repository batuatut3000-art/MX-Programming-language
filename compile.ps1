param([string]$SourceFile = 'main.mx')
$ErrorActionPreference = 'Stop'

Push-Location -LiteralPath $PSScriptRoot
try {
    if (-not $SourceFile.EndsWith('.mx', [StringComparison]::OrdinalIgnoreCase)) {
        $SourceFile += '.mx'
    }
    if (-not (Test-Path -LiteralPath $SourceFile -PathType Leaf)) {
        throw "MX source not found: $SourceFile"
    }

    $python = Get-Command python -ErrorAction SilentlyContinue
    $pythonArgs = @()
    if (-not $python) {
        $python = Get-Command py -ErrorAction SilentlyContinue
        $pythonArgs = @('-3')
    }
    if (-not $python) { throw 'Python 3 is required.' }
    $clang = Get-Command clang -ErrorAction SilentlyContinue
    $linker = Get-Command lld-link -ErrorAction SilentlyContinue
    if (-not $clang -or -not $linker) { throw 'Clang and lld-link are required in PATH.' }

    $name = [IO.Path]::GetFileNameWithoutExtension($SourceFile)
    New-Item -ItemType Directory -Force -Path 'build/EFI/BOOT' | Out-Null
    & $python.Source @pythonArgs 'compiler/mxc.py' $SourceFile 'build/generated.c'
    if ($LASTEXITCODE -ne 0) { throw 'MX compiler failed.' }

    $flags = @('-target', 'x86_64-pc-win32', '-ffreestanding', '-fshort-wchar',
               '-fno-stack-protector', '-fno-builtin', '-O2', '-Iruntime')
    $units = @('mx_runtime', 'mx_lowlevel', 'mx_http', 'mx_browser_text',
               'mx_browser_layout', 'mx_browser_js', 'mxWiFi', 'mxEthernet')
    $objects = @()
    foreach ($unit in $units) {
        $object = "build/$unit.obj"
        & $clang.Source @flags -c "runtime/$unit.c" -o $object
        if ($LASTEXITCODE -ne 0) { throw "C compilation failed: $unit" }
        $objects += $object
    }
    & $clang.Source @flags -c 'build/generated.c' -o 'build/generated.obj'
    if ($LASTEXITCODE -ne 0) { throw 'Generated C compilation failed.' }
    $objects += 'build/generated.obj'

    & $linker.Source '/subsystem:efi_application' '/entry:efi_main' '/nodefaultlib' '/out:build/BOOTX64.EFI' @objects
    if ($LASTEXITCODE -ne 0) { throw 'EFI linking failed.' }
    Copy-Item -LiteralPath 'build/BOOTX64.EFI' -Destination 'build/EFI/BOOT/BOOTX64.EFI' -Force
    & $python.Source @pythonArgs 'scripts/make_uefi_iso.py' 'build' $name
    if ($LASTEXITCODE -ne 0) { throw 'ISO packaging failed.' }
    Write-Host "Ready: build/$name.iso"
}
finally {
    Pop-Location
}
