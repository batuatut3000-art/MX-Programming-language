# kakaOS 0.8 — MXPL UEFI desktop prototype

Build from the `MX` directory. Linux/WSL and macOS use `bash compile.sh main.mx`; Windows PowerShell uses `.\compile.ps1 main.mx`. The output is `build/main.iso`. Another file, such as `examples/wifi.mx`, produces `build/wifi.iso`. With no argument the script uses `main.mx`. See [installation](docs/installation.md) for tools and commands on each host system.

To run the desktop in QEMU with a virtual network adapter, entropy device, and USB tablet, use `bash run-qemu.sh main` on Linux/macOS or `.\run-qemu.ps1 main` on Windows after building. The launchers find a common OVMF location or accept `MX_OVMF_CODE`.

For a physical UEFI PC, format a USB drive as FAT32 and copy `build/EFI/BOOT/BOOTX64.EFI` onto it at the same path. Select the USB drive in the UEFI boot menu. This binary is unsigned, so Secure Boot must allow unsigned applications (or the binary must be signed with a trusted key). Back up the USB drive before formatting. The ISO is for UEFI optical boot; it is not a raw USB disk image.

Mouse speed is set in MXPL with `mouse(dpi=1600)` or `dpi = 1600` followed by `mouse(dpi=dpi)`. `800` is normal speed, `1600` is 2x; accepted values are 100–12800. `mouse()` keeps the previous 1600 default. This changes cursor speed, not the hardware sensor DPI. On a physical PC, MXOS first uses UEFI Simple/Absolute Pointer protocols; it also tries the UEFI USB I/O protocol for standard boot-protocol USB HID mice without a pointer driver. USB I/O requires the firmware to expose the device; unusual HID report formats and hardware without UEFI USB services are not supported.

Positions and sizes can use percentages: `50%` means half the current screen width for X or width, and half the current screen height for Y or height. This works in `rect`, `text`, `print class`, `move`, and horizontal `animate`. `radius(20%)` uses 20% of the rectangle's shorter side. For example, `rect class"button"(10%,20%,25%,12%,rgb(30,100,255),radius(20%))`. Pixel values remain valid, including the older `px`, `x`, and `y` suffixes. A numeric variable assigned `Step = 10%` keeps percent units when used for `move` or `animate`.

For QEMU mouse testing, boot the new ISO with `-device qemu-xhci,id=xhci -device usb-tablet,bus=xhci.0`. QEMU translates input from either a host USB mouse or a built-in touchpad into this virtual tablet; the guest does not see which physical host device was used. kakaOS polls every UEFI Simple/Absolute Pointer device and can also read QEMU's tablet HID report directly. If your OVMF build has no USB pointer driver, remove both tablet options to use the QEMU PS/2 mouse. A host cursor visible over the QEMU window does not mean the guest received mouse input.

The bottom-left `mouse: ... 0000` indicator shows the last input source and count of received movement/click events. Move the mouse inside QEMU; the number should increase. If it remains zero, the guest is not receiving events from that source.

The bottom row also shows `USB io:N hid:N on:N tab:N fail:N err:N`. `io` counts USB interfaces exposed by UEFI, `hid` counts boot-protocol mouse interfaces found, `on` counts active USB mouse readers, `tab` means a QEMU USB tablet was detected for direct report polling, `fail` counts setup/restart failures, and `err` counts failed USB transfers. USB reports are checked before built-in pointer devices so a busy touchpad cannot hide USB mouse input. The pointer is a small black arrow with a white outline.

VS Code: install `vscode-mxpl/mxpl-language-0.0.5.vsix` with **Extensions: Install from VSIX...**. To rebuild the extension, run `python3 vscode-mxpl/package_vsix.py`. It adds MXPL syntax highlighting and `.mx` file recognition.

## Wi-Fi on the kakaOS desktop

`main.mx` has a Wi-Fi icon in the dock and a `wifi` shell command. Opening either starts a scan, shows nearby SSIDs and signal quality, and lets you select a network with the mouse or keys `1`–`9`. Enter its password; leave it blank for an open network. Password input is masked. Press `R` to scan again or Escape to close. The Wi-Fi API is also available to MXPL programs:

```mx
Count = wifi_scan()
if Count > 0:
    Network = wifi_ssid(0)
    Password = password_input("Wi-Fi password> ")
    Result = wifi_connect(Network, Password)
    if Result == 0:
        print("Wi-Fi connected")
```

`wifi_count()` returns the last scan count, and `wifi_quality(0)` returns signal quality for the first result. `wifi_manager()` opens the desktop Wi-Fi screen; `examples/wifi.mx` demonstrates it. Password connections use the UEFI Wireless MAC Connection II and Supplicant protocols. They require UEFI drivers that expose the physical Wi-Fi adapter; an ordinary QEMU virtual NIC appears as Ethernet, so it cannot list the host's Wi-Fi networks. Without those protocols kakaOS displays a clear message instead of an empty list. WPA-PSK is the intended password mode; enterprise EAP networks and firmware-specific Wi-Fi drivers are outside this implementation.

## mxEthernet and mxWiFi adapters

The Ethernet icon in `main.mx` opens a list of adapter MAC addresses and link states; press `R` to refresh or Escape to close. The `ethernet` shell command opens the same screen. `runtime/mxEthernet.c` discovers network cards through UEFI Simple Network Protocol (SNP), so it works with cards for which the firmware has an SNP driver. `runtime/mxWiFi.c` scans SSIDs and requests a connection through UEFI Wireless MAC Connection II and Supplicant. These are firmware protocol adapters, not drivers for every PCI/USB Wi-Fi or Ethernet chipset. A card without compatible UEFI support needs a chipset-specific driver. QEMU's `e1000` is an Ethernet device and does not expose the host Wi-Fi SSIDs.

MXPL exposes read-only discovery and raw Ethernet frames:

```mx
[uefi]
Adapters = ethernet_scan()
if Adapters > 0:
    Address = ethernet_mac(0)
    Link = ethernet_link(0)
    print(Address)
    print(Link)
```

`ethernet_count()` gives the cached count. To use raw frames, call `Result = ethernet_open(0)`, allocate a buffer with `alloc`, write an entire Ethernet frame with `poke8` (destination MAC, source MAC, EtherType, payload), and call `Result = ethernet_send(0, Buffer, Length)`. `Bytes = ethernet_receive(0, Buffer, Capacity)` returns a packet length, zero if no packet is ready, or a negative error. Call `Result = ethernet_close(0)` when done. Raw frames require a valid interface and do not by themselves provide IP, DHCP, DNS or HTTP. Avoid raw access while the firmware HTTP client is using the same adapter. See `examples/ethernet.mx` for discovery.

The browser still uses firmware HTTP services and therefore needs a firmware network stack and suitable drivers. The Ethernet manager reports `link unknown` if the firmware does not provide media state.

## Low-level MXPL, web, and flat binaries

The same four-space indentation and simple calls now expose x86-64 hardware operations. Numbers may be decimal, `0x` hexadecimal, or `0b` binary. Numeric variables support `+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `<<`, `>>`, `~`, and comparisons in `if`. `print(Value)` displays a decimal number. These operations run in UEFI firmware context with no memory protection; use valid mapped addresses and hardware ports.

```mx
[uefi]
Port = 0x80
out8(Port, 0x55)
Value = in8(Port)
print(Value)
Buffer = alloc(256)
if Buffer:
    poke8(Buffer, Value | 1)
    Byte = peek8(Buffer)
    free(Buffer)
sleep(10ms)
```

`peek16`, `peek32`, `poke16`, and `poke32` are also available. `alloc(size)` returns a UEFI pool address (or zero on failure), and `free(address)` releases it. `runbin("HELLO.BIN")` opens a flat x86-64 binary from an EFI filesystem volume, loads it into executable UEFI LoaderCode memory, calls byte 0 as a Microsoft x64 ABI function with no arguments, and shows its integer return value. `Result = runbin("HELLO.BIN")` stores it. The sample `build/HELLO.BIN` returns 42. Put your own 8.3-named `.bin` files in `bins/` before building; the ISO includes them in its EFI boot filesystem. On a physical FAT32 USB drive, copy the `.bin` files to its root alongside the `EFI` directory. The maximum binary size is under 1 MiB. These are flat position-independent machine-code files, not ELF/PE executables. They execute with full firmware privileges and must be trusted.

`browser("http://example.com/")` opens a graphical page viewer. It draws page backgrounds, headings, wrapped paragraphs and clickable links using a small HTML/CSS subset. Click the address bar to enter another URL, the back arrow to return, or X/Escape to close; Up/Down scroll and R reloads. `main.mx` also has a `web` shell command that asks for a URL and a `bin` command that asks for a binary filename. The browser uses UEFI HTTP Service Binding and HTTP protocols, so firmware must provide a network driver, DHCP/DNS/TCP/HTTP support, and TLS for `https://`. It follows HTTP redirects and displays the first 32 KiB of a response. Inline JavaScript supports string variables and concatenation, `document.write`/`writeln`, `document.title = ...`, and simple `document.getElementById(...).textContent` / `.innerHTML` assignments. It does not fetch external scripts or support general DOM APIs, event handlers, arrays, loops, modern JavaScript frameworks, images, forms or full HTML/CSS. A failed request displays its UEFI status code. In QEMU, add `-nic user,model=e1000`; the OVMF build must include network, HTTP and TLS drivers.

`examples/lowlevel.mx` shows `runbin` and `browser`. Build it with `bash compile.sh examples/lowlevel.mx`, then boot `build/lowlevel.iso`.

Clicking Browser on the desktop displays a URL entry overlay even when MX shell is closed. Enter a full URL and press Enter, or press Escape to cancel. The browser requests an IPv4 address with UEFI IP4 Configuration II/DHCP before making an HTTP request; this can take up to ten seconds and can be cancelled with Escape. The UEFI HTTP request includes a Host header, which the EDK II HTTP implementation requires. The viewer decodes complete HTTP chunked bodies. Each UEFI HTTP request/response phase has a six-second wait limit for HTTP and a fifteen-second limit for HTTPS. QEMU must be started with `-nic user,model=e1000` (included in `run-qemu.sh`), and OVMF must expose the network, HTTP and TLS drivers. If the browser reports `IPv4/DHCP unavailable`, the firmware has not provided an address.

For HTTPS, `run-qemu.sh` passes an EDK II CA database to OVMF. It uses `MX_TLS_CERTS` if set, otherwise `/etc/pki/ca-trust/extracted/edk2/cacerts.bin`, or asks `p11-kit` to generate `build/cacerts.bin`. On Debian/Ubuntu, install `p11-kit` and a CA trust package, then run `bash run-qemu.sh main`. The CA database must be in `edk2-cacerts` format; a PEM file is not accepted by OVMF. The firmware still needs an HTTPS-capable HTTP/TLS driver. No insecure certificate bypass is used.

Use `bash run-qemu.sh main --https` to require a CA database before starting QEMU. If your OVMF image does not provide the UEFI TLS service, supply one built with `NETWORK_HTTP_BOOT_ENABLE` and `NETWORK_TLS_ENABLE` through `MX_OVMF_CODE=/path/to/OVMF_CODE.fd bash run-qemu.sh main --https`. A UEFI `0x15` status means the request was aborted; the browser now checks whether the TLS protocol and CA variable are present and shows the missing prerequisite. A CA variable being present does not prove that the server certificate or TLS handshake will succeed.

`scripts/make_ca_db.py` converts a trusted PEM bundle to OVMF's EFI X.509 signature-list format and validates its framing. Example: `python3 scripts/make_ca_db.py /etc/ssl/certs/ca-certificates.crt build/cacerts.bin`. A local `build/cacerts-certifi.bin` can be used as a fallback, but `build/` is ignored by Git. On a fresh checkout the launcher creates a CA database from the system trust store or `p11-kit`. Refresh the database from your system's trusted CA bundle when certificates change; the generator does not invent or disable certificate trust. The browser's `0x15` error with TLS and CA present still indicates a handshake failure, whose exact cause may require firmware logs.

To try the supported JavaScript subset, run `python3 -m http.server 8000` from this directory, open the browser in QEMU, and enter `http://10.0.2.2:8000/examples/js-demo.html` (the QEMU user-network host address). The page text should change to `JavaScript works here`, a generated paragraph should appear, and the tab should read `JavaScript active`.

## Features
- Graphical desktop (UEFI GOP framebuffer), named rectangles and text objects
- `mouse(dpi=1600)` shows a cursor for UEFI pointer devices and supported USB boot mice; `if click("appicon"):` handles a left click
- `Command = poll_input("KAKA> ")` reads keyboard input without blocking mouse events
- Graphical objects are declared with `rect class"name"(...)`; there is no built-in terminal
- `shutdown()` invokes UEFI Runtime Services ResetSystem(EfiResetShutdown), not Multiboot2 ACPI
- `def` function blocks, `show`, `hide`, `animate("name",200x,1000ms)` and raw `__C__:` blocks supported in compiler subset
- `print("text")`, `Command = input("prompt")`, and `if Command = animation:` for simple keyboard commands
- `print class"button"(200x, 100y"KAKA")` places text at screen position `(200,100)` and attaches it to `button`, so it moves with the rectangle
- `text class"title"(24,16,"Hello",rgb(255,255,255))` creates a named text object; `show`, `hide`, and `animate` work with its name
- `if`, `elif`, `else`, `while True`, and `def` use four-space indentation; errors include source file and line number
- `if Command:` runs a block only after `poll_input` returns a nonempty command, so an idle loop does not redraw the entire desktop
- `main.mx` is a mouse and keyboard desktop: click the shell icon to open, X to close, POWER to shut down, or the blue tile to animate it
- `if key("Escape"):` and `if key("Enter"):` inspect the key that ended the last `input()` call
- `Step = 30x` creates a numeric variable; `move("button", Step, 0y)` moves an object immediately
- `__C__:` adds C definitions before `mx_main`

## Limitations
- UEFI x86-64 only. BIOS / Multiboot2 ACPI code is not integrated.
- `__C__:` injects C code into the generated translation unit and is **unsafe for untrusted source**.
- `animate` redraws only the changed area and uses UEFI Stall; it is blocking and duration includes additional rendering time.
- Simple Pointer movement follows the UEFI screen-size/resolution recommendation with a configurable DPI speed multiplier and fractional movement retained between polls. Cursor redraw clips large rectangles to the small changed area.
- Keyboard input uses UEFI text input and supports ASCII.
- `key()` currently recognizes Escape and Enter after `input()` or `poll_input()`.
- The PS/2 fallback accesses x86 I/O ports directly; it is intended for the QEMU `pc` machine and may not work on other firmware or hardware.
- USB fallback supports boot-protocol HID mice exposed through UEFI USB I/O. It is not a native xHCI driver, and has not been checked on physical hardware.
- This UEFI application does not yet have an independent kernel, process scheduler, persistent file system, or device drivers.
- HTTP support and x86-64 flat binary execution are firmware-stage features. Internet access and physical USB mouse support depend on the UEFI drivers available on the machine.
- Rendering is designed for a 1024x768 or larger GOP mode. It does not set a screen resolution.
- Compilation and ISO structure were checked; graphical boot was not verified in QEMU in this environment.

## MX code sample
```
[uefi]
clear(rgb(12,20,35))
dpi = 1600
mouse(dpi=dpi)
rect class"button"(10%, 20%, 25%, 12%, rgb(30,100,255), radius(20%))
text class"title"(10%, 15%, "Hello", rgb(255,255,255))
```

`__C__:` accepts indented C definitions, for example:
```
__C__:
    static int my_c_function(void) { return 42; }
```
These functions are compiled with the generated C; calling arbitrary C functions directly from MX statements is not yet implemented.



