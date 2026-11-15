# Vergil

Low-latency, synchronized audio streaming over LAN multicast, written in C++20.

One **sender** captures audio from an input device and multicasts it as UDP packets. Any number of **receivers** play it, all at the same moment: each packet carries a presentation time on the sender's clock, every receiver locks to that clock with a PTP-style protocol, and an adaptive resampler steers playback so that each frame leaves the speaker exactly when it is due. Vergil runs on macOS, Linux and Windows.

## How it works

```
 sender                                         receiver (one or many)
┌──────────────────────────────┐               ┌────────────────────────────────────┐
│ capture ─► time filter (DLL) │   audio       │ packets ─► frame ring              │
│     │      stamps t = capture│ ─────────────►│              │                     │
│     │      + latency L       │ 239.255.0.1   │ presentation timeline (t ↔ frame)  │
│     ▼                        │   :12345      │              │                     │
│ packet ring ─► send thread   │               │ rate loop ─► resampler ─► device   │
│                              │   clock sync  │              ▲                     │
│ clock-sync master            │ ◄────────────►│ clock-sync slave (offset, delay)   │
└──────────────────────────────┘ 239.255.0.2   └────────────────────────────────────┘
                                   :12346
```

- **Clock sync.** The sender is the clock master. Receivers exchange Sync, DelayReq and DelayResp messages with it, as in PTP, and a servo with outlier gating estimates the offset to the master clock. Receive timestamps are taken in user space by default; with `--kernelTimestamps` they come from the kernel where the platform offers it (`SO_TIMESTAMPNS` on Linux, `SO_TIMESTAMP_MONOTONIC` on macOS, `SIO_TIMESTAMPING` on Windows).
- **Time filters.** Audio callbacks arrive with jitter. A delay-locked loop (DLL) smooths the callback times of the sender's capture device and the receiver's playback device into clean, drift-tracking clocks.
- **Presentation time.** The sender stamps each packet with the time its first frame should leave the speakers: the capture time plus a fixed latency `L` (100 ms by default). This is the same moment for every receiver.
- **Adaptive resampling.** On each callback, the receiver works out which input frame should be playing now, compares it with the frame about to be played, and adjusts the resampling ratio by at most ±0.5 %. A windowed-sinc resampler with fractional position tracking applies the ratio. Errors larger than 5 ms trigger a re-alignment instead of a slow correction.
- **Real-time friendly.** The audio threads do not lock or allocate. Data moves between threads through lock-free single-producer/single-consumer rings, and the audio and network threads ask the OS for real-time priority.

The control loops follow Fons Adriaensen's papers, which are included in [`docs/`](docs):

- [Using a DLL to filter time](docs/usingdll.pdf)
- [Controlling adaptive resampling](docs/adapt-resamp.pdf)

## Requirements

- A C++20 compiler (Clang, GCC or MSVC)
- CMake 3.21 or later
- [Conan 2](https://conan.io) to install the dependencies: [Asio](https://think-async.com/Asio/), [miniaudio](https://miniaud.io), [CLI11](https://github.com/CLIUtils/CLI11) and [Catch2](https://github.com/catchorg/Catch2)
- A network that carries IPv4 multicast between sender and receivers
- Optional: Python 3 for clang-tidy and the playout comparison tool (which also needs NumPy)

## Building

```sh
# 1. Install dependencies and generate the toolchain and presets
conan install . --output-folder=build --build=missing -s build_type=Release

# 2. Configure
cmake --preset conan-release -DVERGIL_NATIVE_ARCH=ON

# 3. Build
cmake --build --preset conan-release

# 4. Run the tests
ctest --preset conan-release --output-on-failure
```

With a single-config generator (Makefiles, Ninja), the binaries end up in `build/build/Release/<target>/`, for example `build/build/Release/sender/sender`.

### CMake options

| Option | Default | Effect |
| --- | --- | --- |
| `VERGIL_NATIVE_ARCH` | `OFF` | Optimizes release builds for the host CPU (`-march=native`). The binaries are not portable. |
| `VERGIL_WARNINGS_AS_ERRORS` | `OFF` | Treats compiler warnings as errors. |
| `VERGIL_SANITIZE_THREAD` | `OFF` | Builds with ThreadSanitizer. For development only; use a separate build directory. Not supported with MSVC. |
| `VERGIL_CLANG_TIDY_EXE` | auto | Path to clang-tidy. By default it is found in `.venv`, then in `PATH`. |

Release builds use link-time optimization when the toolchain supports it.

## Usage

### 1. Find devices and interfaces

```sh
sender   --enumInputDevices        # audio inputs, by index
receiver --enumOutputDevices       # audio outputs, by index
sender   --enumNetworkInterfaces   # multicast-capable interfaces
```

### 2. Start the sender

```sh
sender -i 0 -n en0
```

### 3. Start one or more receivers

```sh
receiver -o 1 -n en0 -s 48000
```

The receiver prints a status line with clock-sync quality, buffer fill, and loss and timing counters. Stop either program with Ctrl+C.

> **Note:** The packets carry neither the sample rate nor the channel count. Pass the sender's capture rate to each receiver with `-s` (the sender prints it at start-up), and use devices with the same number of channels on both sides. If the channel counts differ, the receiver counts every packet as a size mismatch.

### Sender options

| Option | Default | Description |
| --- | --- | --- |
| `-i, --inputDeviceIndex` | required | Index of the audio input device |
| `-L, --latency` | `100` | Milliseconds from capture to the receivers' speakers, the same for every receiver. Must stay below about 150 ms, the receiver's buffer. |
| `-n, --networkInterface` | auto | Network interface to send on. Required if more than one interface qualifies. |
| `-m, --multicastGroup` | `239.255.0.1` | Multicast group for audio |
| `-p, --multicastPort` | `12345` | Multicast port for audio |
| `--kernelTimestamps` | off | Clock sync: use kernel receive timestamps where available |
| `--testSignal` | off | Sends a 10 ms noise burst at the start of every second instead of the input, for measurements |

### Receiver options

| Option | Default | Description |
| --- | --- | --- |
| `-o, --outputDeviceIndex` | required | Index of the audio output device |
| `-s, --senderSampleRate` | `48000` | The sender's sample rate |
| `-f, --periodSizeInFrames` | `240` | Period size in frames |
| `--outputLatency` | `0` | Milliseconds from the audio callback to the sound leaving the speaker. Measure it per receiver: miniaudio does not report it reliably. |
| `--log` | none | Writes a CSV row per callback: when it is heard and how far off it plays |
| `-n, --networkInterface` | auto | Network interface to receive on. Required if more than one interface qualifies. |
| `-m, --multicastGroup` | `239.255.0.1` | Multicast group for audio |
| `-p, --multicastPort` | `12345` | Multicast port for audio |
| `--kernelTimestamps` | off | Clock sync: use kernel receive timestamps where available |

Clock sync always uses `239.255.0.2:12346`. Turn on `--kernelTimestamps` on all nodes or on none: if only one side uses kernel stamps, the path delay is measured asymmetrically, which biases the clock offset.

On Linux, real-time thread priority needs `CAP_SYS_NICE` or a suitable `rtprio` limit. Without it the threads keep their normal priority and the programs still run.

### Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Success |
| `1` | Network error: no usable interface, or a socket or the clock sync failed |
| `2` | Audio error: no usable device, or it failed to open or start |
| `3` | A file to write could not be opened |

## Measuring synchronization

To see how closely receivers on **one host** play together:

```sh
sender   -i 0 --testSignal
receiver -o 1 -s 48000 --log a.csv
receiver -o 2 -s 48000 --log b.csv

python3 tools/compare_playout.py -s 48000 a.csv b.csv
```

On a single host, local time and master time are the same, so the script can separate each receiver's clock-sync error from its rate-loop error and report the offset between receivers over time. Output latency does not appear in the logs. To include it, record the speakers and cross-correlate the test signal's noise bursts.

## Project layout

| Path | Contents |
| --- | --- |
| [`sender/`](sender) | Capture, packetizing and the clock-sync master |
| [`receiver/`](receiver) | Reception, presentation timeline, rate loop, resampling and playback, plus the status line |
| [`clocksync/`](clocksync) | PTP-style clock synchronization: messages, servo and timestamped sockets |
| [`streaming/`](streaming) | Audio packet header encoding and decoding |
| [`netint/`](netint) | Network interface enumeration and multicast socket setup |
| [`include/`](include) | Shared headers: lock-free rings, time filter, resampler, rate loop and more |
| [`tests/`](tests) | Catch2 unit tests |
| [`tools/`](tools) | `compare_playout.py` and the clang-tidy report script |
| [`dll/`](dll) | Experiment that applies the DLL time filter to a playback device's callbacks |
| [`clocksync-demo/`](clocksync-demo) | Runs clock sync on its own, as master or slave |
| [`netint-demo/`](netint-demo) | Lists interfaces and opens a multicast sender and receiver |
| [`docs/`](docs) | Background papers |

## Development

### clang-tidy

The checks are configured in [`.clang-tidy`](.clang-tidy). The commands below install the pinned clang-tidy into `.venv`, where CMake finds it without activating the venv.

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements-dev.txt     # Windows: .venv\Scripts\pip
cmake --preset conan-release

# Checks all sources and writes a report, each warning once, to build/build/Release/clang-tidy.log
cmake --build --preset conan-release --target clang-tidy
```

The target needs a Makefile or Ninja generator. On Windows, add `-c tools.cmake.cmaketoolchain:generator=Ninja` to the `conan install` step and run from a Developer Command Prompt.

## License

Vergil is licensed under the [GNU General Public License v3.0](LICENSE).
