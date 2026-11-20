## Changes

$changes

## Compatibility

- macOS $minimum_macos or later.
- Library ABI `$abi`.

## Install or upgrade

Choose the installer for your Mac from **Assets**:

- Intel: `libxldripper-$version-macos-x86_64.pkg`
- Apple Silicon: `libxldripper-$version-macos-arm64.pkg`

The package installs the library, development headers, and `xld-rip` under
`/opt/libxldripper`. These packages are unsigned and are not notarized.

Verify the installed version with `/opt/libxldripper/bin/xld-rip --version`.
Expected output: `xld-rip $version (ABI $abi)`.
