<!-- Squid icon -->
<p align="center">
  <img src="https://gitlab.athena.michelbarnich.com/uploads/-/system/project/avatar/21/Squid-iOS-Default-1024_1x.png?v=1791988520&width=180" alt="Squid icon" width="180">
</p>

<h1 align="center">Squid</h1>

<p align="center">
  A macOS tweak manager and Objective-C method-hooking runtime.
</p>

<p align="center">
  <strong>Configure tweaks in a native macOS app. Load them through a privileged LaunchDaemon.</strong>
</p>

---

## Overview

Squid is a macOS project composed of three pieces:

- **Squid Manager** — a SwiftUI app for creating, editing, enabling, and removing plist-backed tweak configurations.
- **libsquid** — a small C/Objective-C runtime library that exposes helpers for replacing Objective-C instance and class methods.
- **squidd** — a native daemon that loads the configured tweaks and keeps the runtime active.

Tweak definitions are stored in `~/.squid` as XML property lists. The bundled installer registers `squidd` as `com.squid.squidd` in macOS's system LaunchDaemon domain.

## Features

- Native SwiftUI manager for macOS
- Plist-backed tweak configuration
- Add, edit, refresh, and remove tweak entries
- Instance-method and class-method hooks
- Convenience APIs that resolve classes and selectors by name
- Self-contained `Squid.app` packaging
- Explicit administrator authorization for privileged installation
- LaunchDaemon installation and removal scripts

## Architecture

```text
┌──────────────────────┐
│    Squid Manager     │  SwiftUI configuration UI
└──────────┬───────────┘
           │  ~/.squid/*.plist
           ▼
┌──────────────────────┐
│       squidd         │  Privileged daemon
└──────────┬───────────┘
           │  loads configured tweaks
           ▼
┌──────────────────────┐
│      liblibsquid     │  Objective-C runtime hooks
└──────────────────────┘
```

The final application bundle embeds the manager, daemon, dynamic library, LaunchDaemon definition, and install/uninstall commands so it can be moved as one unit.

## Requirements

- macOS
- Xcode with the macOS SDK
- Administrator access for installing or removing the daemon
- Debugger restrictions disabled when using Squid's process inspection and injection features

### Debugger restrictions

Some Squid features require macOS debugger restrictions to be disabled. Check the current System Integrity Protection configuration from Terminal:

```sh
csrutil status
```

If the reported configuration still enables the debugger restrictions Squid needs, follow Apple's official [System Integrity Protection guide](https://support.apple.com/guide/security/system-integrity-protection-secb7ea06b49/web) for the supported RecoveryOS procedure and the security implications of changing it. Re-enable the protections when you no longer wish to use Squid.

## Build

Open the Xcode project, select the shared **Squid** scheme, and choose **Product → Build**.

Or build from Terminal:

```sh
xcodebuild \
  -project Squid.xcodeproj \
  -scheme Squid \
  -configuration Release \
  -derivedDataPath .build/DerivedData \
  build
```

The build produces these release products:

```text
Products/
├── Squid.app
├── squidd
└── liblibsquid.dylib
```

## Package the app

After building the Release products, assemble the self-contained application bundle:

```sh
sh Packaging/build-package.sh
```

The packaged app is written to:

```text
Products/Squid.app
```

To use a different product or output directory:

```sh
PRODUCTS_DIR=/path/to/products \
OUTPUT_DIR=/path/to/output \
sh Packaging/build-package.sh
```

For signed distribution builds, set `CODE_SIGN_IDENTITY` before running the packaging script. Without one, the script uses ad hoc signing for local development.

## Install

1. Move `Products/Squid.app` to a location of your choice.
2. Launch **Squid Manager**.
3. Click **Install Squidd**.
4. Approve the administrator authorization prompt.

The installer copies the daemon and library to:

```text
/Library/Application Support/Squid/
```

and registers:

```text
/Library/LaunchDaemons/com.squid.squidd.plist
```

The daemon runs as root and is configured to be managed by `launchd`.

You can also run the bundled installer directly:

```sh
open Products/Squid.app/Contents/Resources/Squid/install-squidd.command
```

## Configure a tweak

From Squid Manager, choose **Add Tweak** and provide:

| Field | Description |
| --- | --- |
| **Identifier** | A stable name for the tweak |
| **Target bundle ID** | The application or process the tweak targets |
| **OS version** | Optional version constraint; leave blank for all versions |
| **Dylib path** | Path to the tweak dynamic library |
| **Enabled** | Whether Squid should load the tweak |

The resulting configuration is saved as an XML plist in `~/.squid`.

## Remove Squid

Use the bundled uninstaller:

```sh
open Products/Squid.app/Contents/Resources/Squid/uninstall-squidd.command
```

This stops the LaunchDaemon and removes its files from `/Library/Application Support/Squid` and `/Library/LaunchDaemons`.

## Runtime API

`libsquid` exposes C-callable helpers for Objective-C runtime method replacement:

```c
bool squid_hook_instance_method_named(
    const char *class_name,
    const char *selector_name,
    IMP replacement,
    IMP *original_implementation
);

bool squid_hook_class_method_named(
    const char *class_name,
    const char *selector_name,
    IMP replacement,
    IMP *original_implementation
);
```

The previous implementation can be captured through `original_implementation`, allowing a replacement to call through to the original behavior when appropriate.

## Security notes

Squid installs a root LaunchDaemon and can load arbitrary dynamic libraries into configured targets. Only install tweaks and libraries you trust.

- Review each tweak's plist and library path before enabling it.
- Keep administrator authorization explicit.
- Test tweaks on a non-critical system first.
- Removing Squid requires administrator authorization because it removes system-level files.

## Project layout

```text
Squid/
├── Squid Manager/       SwiftUI manager application
├── libsquid/            Objective-C runtime hook library
├── squidd/              Privileged daemon and Mach-O loader
├── Packaging/           Installer, uninstaller, and package assembly
├── Products/            Build products and packaged app
└── Squid.xcodeproj      Xcode project
```

## License

No license file is currently included in this repository. Add one before distributing Squid or accepting external contributions.
