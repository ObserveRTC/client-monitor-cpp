# AGENTS.md

Guidance for AI coding agents (and humans) working in this repository.

## What this is

A native C++17 port of [`@observertc/client-monitor-js`](https://github.com/ObserveRTC/client-monitor-js):
it collects W3C WebRTC stats, keeps a monitor per stats object, runs detectors, and produces
ObserveRTC `ClientSample`s. The JS library is the reference implementation. When behaviour is in
doubt, read the JS source and match it; when the C++ deliberately differs, record it in
`PORTING.md` under "Where the C++ differs on purpose".

## Build and test

```sh
cmake -S . -B build -DCMAKE_COMPILE_WARNING_AS_ERROR=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Before finishing a change, also run the sanitizer builds CI runs:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=thread"
cmake --build build-tsan -j && ctest --test-dir build-tsan --output-on-failure
```

and the consumer check when touching `CMakeLists.txt` or the public include layout:

```sh
cmake -S test/consumer -B build-consumer -DCLIENTMONITOR_SOURCE_DIR=$PWD
cmake --build build-consumer && ./build-consumer/consumer
```

The bindings (`CLIENTMONITOR_LIBWEBRTC`, `CLIENTMONITOR_MEDIASOUPCLIENT`) need a libwebrtc build and
are not compiled in CI. Keep them minimal and say so when you change them untested.

## Layout

- `include/clientmonitor/`: public headers, included as `<clientmonitor/...>` or
  `"clientmonitor/..."`. Every project include uses that prefix.
- `src/`: the core. `bindings/`: binding sources, built only when enabled.
- `include/clientmonitor/schema/ClientSample.hpp`, `Samples.hpp`, `JsonSupport.hpp`: **generated**
  by observertc-schemas (`npm run generate:cpp`, `outputs/cpp/`). Never edit them here; regenerate
  there and copy them over. `SchemaTypes.hpp` beside them is hand-written.
- `test/src/*.test.cpp`: Catch2 v2 tests. `test/consumer/`: FetchContent consumer.
- `PORTING.md`: what is ported from JS, what is not, and the deliberate differences. Keep it current.

## Conventions

Follow libmediasoupclient's style (see the README's Conventions section):

- `PascalCase` methods, `camelCase` members, `this->` on every member access, tabs, Allman braces;
  format with the repository's `.clang-format`.
- Events go through nested abstract `Listener` classes with default no-op virtuals
  (`ClientMonitor::Listener`). Do not introduce an event emitter or `std::function` signals.
- Every `.cpp` starts with `#define CM_CLASS "ClassName"` and logs through `CM_DEBUG` / `CM_WARN` /
  `CM_ERROR`. Headers never include `Logger.hpp`.
- Free-form data is `nlohmann::json`. A monitor exposes the raw report through `GetStats()` and its
  derived values through a `Metrics` struct from `GetMetrics()`; detector outcomes go in `Verdicts`.
- Optional values are `std::optional`, never sentinel numbers, mirroring the JS `undefined`.
- Comments explain why, as the JS sources do; keep the JS reasoning when porting.

## Porting a detector

1. Read the JS detector in `client-monitor-js/src/detectors/` and its section in `docs/`.
2. Add its config struct to `ClientMonitorConfig.hpp` as `std::optional<...>` with the JS defaults
   (`std::nullopt` disables it, the JS `null`).
3. Add the class under `include/clientmonitor/detectors/` and `src/detectors/`, register it where
   the owning monitor's constructor says (client, peer connection, ICE transport or track), and add
   the source to `CMakeLists.txt`.
4. If the JS detector emits an event, add a default no-op method to `ClientMonitor::Listener`.
5. Add tests to `test/src/Detectors.test.cpp` that drive it with scripted stats
   (`helpers::BuildReport`, `helpers::FakeClock`), and move it to "ported" in `PORTING.md`.

## Threading rules to preserve

- Public methods take the monitor's recursive mutex. `StatsCollector::GetStats()` must stay outside
  it: the collector blocks on libwebrtc's signaling thread, which may be calling into the monitor.
- Listener callbacks run with the lock held on the collecting thread.
- `IntervalTimer::Stop()` may be called from the timer thread itself (`Close()` from a callback).
  Never join while holding the monitor's lock.

## Don't

- Don't edit the generated schema headers or reintroduce a schema generator here.
- Don't add dependencies beyond nlohmann::json and the standard library to the core.
- Don't change public API names casually: applications pin tags. Note breaking changes in
  `CHANGELOG.md`.
