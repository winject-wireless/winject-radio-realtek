# winject-radio-realtek

Linux radio service for RTL8812AU (`rtl88xxau_wfb`): same m-plane and UDP inject/forward ports as the ESP32 radio.

## Build

```bash
sudo apt install libnl-3-dev libnl-genl-3-dev pkg-config
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

Binary: `build/src/radio/winject-radio-realtek`

## Run

```bash
sudo ./build/src/radio/winject-radio-realtek --config configuration/radio-a.cfg
```

Requires a bound `rtl88xxau_wfb` interface and privileges for `AF_PACKET` / nl80211.

If the dongle is not present yet, the process logs `waiting for device: …` and stays up until it appears (about 1 s debounce after enumeration). m-plane is unavailable until bring-up finishes.

## Layout

See [docs/implementation.md](docs/implementation.md) for module map and bench criteria.
