# libxldripper

`libxldripper` is a macOS library for extracting audio from physical CDs. It
adapts the XLD Secure Ripper from X Lossless Decoder into a C API that streams
CD audio as PCM and reports read diagnostics and checksums.

- Enumerate optical drives and read the disc table of contents (TOC), track
  boundaries, pregaps, and hidden track-one audio (HTOA).
- Extract tracks in Secure or Burst mode. Secure mode supports optional C2
  error pointers. Set a drive read offset or opt into drive speed control.
- Calculate CRC32 with and without zero samples and AccurateRip v1 and v2
  signatures. Check signatures against caller-supplied dBAR records.
- Monitor progress and request cancellation while a track is being read.

## Build and install

Use macOS with the Xcode Command Line Tools or Xcode and CMake 3.20 or later.
The default deployment target is macOS 11.0. Reading discs requires a macOS
compatible optical drive.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
cmake --install build --prefix "$PWD/_install"
```

The default layout is relative to the installation prefix:

| Path | Contents |
| --- | --- |
| `lib/` | `libxldripper.0.0.1.dylib`, its `libxldripper.dylib` symlink, and `libxldripper.a` |
| `include/xld/ripper.h` | Public C header |
| `lib/cmake/libxldripper/` | CMake package |
| `share/libxldripper/` | README and license |
| `bin/xld-rip` | Optional command-line tool |

Configure with `-DLIBXLDRIPPER_BUILD_STATIC=OFF` for a shared-only build or
`-DCMAKE_OSX_ARCHITECTURES='arm64;x86_64'` for universal binaries.

## Link the library

Set `CMAKE_PREFIX_PATH` to the installation prefix when configuring your
application. Link `libxldripper::ripper` for the shared library or
`libxldripper::ripper-static` for the static library. The static target also
links the required macOS frameworks.

```cmake
find_package(libxldripper 0.0.1 CONFIG REQUIRED)
add_executable(rip_stdout rip_stdout.c)
target_link_libraries(rip_stdout PRIVATE libxldripper::ripper)
```

You can also compile the [C example](examples/rip_stdout.c) directly. Use a
media path from the device list in place of `/dev/disk4`.

```sh
prefix="$PWD/_install"
cc examples/rip_stdout.c -I"$prefix/include" -L"$prefix/lib" \
  -lxldripper -Wl,-rpath,"$prefix/lib" -o build/rip_stdout
build/rip_stdout /dev/disk4 1 > track01.pcm
```

## Use the C API

See [`<xld/ripper.h>`](include/xld/ripper.h) for public types, status codes, and
ownership rules. Open a session using the current media path from
`xld_ripper_device_list_create`, then use `xld_ripper_disc_get_info` and
`xld_ripper_track_get_info` to inspect the TOC. The caller manages disc mounting,
ejection, and exclusive access to each physical drive.

The current ABI is `0.0.1`. `xld_ripper_api_version` returns it as a packed
integer with one byte per version component. `xld_ripper_version` returns the
library release as a string.

- `xld_ripper_options_init` defaults to Secure mode, C2 off, a read offset of
  zero, a retry limit of 20, and the drive's current speed. Offsets are in
  stereo PCM frames. Positive values read later on the disc. Samples shifted
  outside the contiguous audio range are filled with zero.
- `xld_ripper_extract_track` reads one track synchronously. Track numbers
  start at 1, with 0 reserved for HTOA. The callback receives interleaved
  signed 16-bit little-endian stereo PCM at 44.1 kHz. The buffer is valid for
  the duration of the callback. Pass a null callback to compute checksums
  without consuming PCM.
- `xld_ripper_get_progress` and `xld_ripper_cancel` can run during extraction.
  Cancellation takes effect between device reads. Use checksums when
  `result.complete` is set. `XLD_RIPPER_DAMAGED` can accompany complete PCM
  when the engine had to choose an unconfirmed sector.
- `xld_ripper_session_set_accuraterip_data` copies and validates supplied dBAR
  records against the disc TOC. AccurateRip network access, metadata lookup,
  and audio file encoding belong to the calling application.

## Optional command-line tool

To build and install `xld-rip`, add `-DLIBXLDRIPPER_BUILD_CLI=ON` to the configure
command above, then rebuild and install.

```sh
_install/bin/xld-rip devices
_install/bin/xld-rip info --device /dev/disk4 --pregaps --probe-c2
_install/bin/xld-rip rip --device /dev/disk4 --track 1 --output track01.pcm
_install/bin/xld-rip verify --device /dev/disk4 --track 1 --accuraterip disc.dBAR.bin
```

`rip` writes raw PCM. `verify` reads the track and prints its checksums and
verification status. Run `xld-rip --help` for the full set of options.

## License and attribution

Original contributions to this repository are licensed under the [Open
Software License 3.0](LICENSE) (`OSL-3.0`). The [upstream XLD project](https://sourceforge.net/projects/xld/)
lists OSL-3.0 at the project level. The derived files retain their upstream
copyright and attribution notices.

**Attribution Notice.** This project incorporates modified code from X Lossless
Decoder (XLD), copyright 2006–2025 tmkk. The CDDA backend, ripper, Secure Ripper
engine, track validator, AccurateRip record parser, and TOC identifier algorithm
were adapted from XLD SVN revision 652 for use as a standalone library.

```sh
svn checkout -r 652 https://svn.code.sf.net/p/xld/code/trunk/ xld-code
```

The [AccurateRip third-party access terms](https://www.accuraterip.com/3rdparty-access.htm)
apply separately to access to the AccurateRip database.
