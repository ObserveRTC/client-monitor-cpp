# client-monitor-cpp

Native C++ port of [`@observertc/client-monitor-js`](https://github.com/ObserveRTC/client-monitor-js) (4.11.0) for WebRTC
clients built on libwebrtc / [libmediasoupclient](https://github.com/versatica/libmediasoupclient).
It collects W3C stats, keeps a monitor per stats object, runs detectors, and produces
ObserveRTC `ClientSample`s (schema **3.7.0**) for the observer.

The API follows libmediasoupclient's conventions: `PascalCase` methods, nested `Listener`
interfaces instead of an event emitter, `nlohmann::json` for free-form data, a `Logger` with
per-file `CM_CLASS` macros, and the same `.clang-format`.

## Add it to your application

client-monitor-cpp is a CMake library, consumed from source the same way libmediasoupclient is.
It needs C++17, CMake 3.14+, threads and nlohmann::json (found or fetched automatically).

```cmake
include(FetchContent)

FetchContent_Declare(
	clientmonitor
	GIT_REPOSITORY https://github.com/ObserveRTC/client-monitor-cpp.git
	GIT_TAG v0.3.0
)
FetchContent_MakeAvailable(clientmonitor)

target_link_libraries(my_app PRIVATE clientmonitor::clientmonitor)
```

```cpp
#include <clientmonitor/ClientMonitor.hpp>
```

Or the release archive, which needs no git on the build machine:

```cmake
FetchContent_Declare(
	clientmonitor
	URL https://github.com/ObserveRTC/client-monitor-cpp/releases/download/v0.3.0/client-monitor-cpp.tar.gz
)
```

Every release also answers at a fixed "latest" URL:

```cmake
FetchContent_Declare(
	clientmonitor
	URL https://github.com/ObserveRTC/client-monitor-cpp/releases/latest/download/client-monitor-cpp.tar.gz
)
```

Prefer a pinned version for production builds. "Latest" is resolved only when a build directory
is first configured; after that FetchContent keeps its copy, so machines with different build
directories can quietly be on different versions, and a breaking release reaches every app at
its next clean build. Each release lists the archive's SHA256 (`client-monitor-cpp.tar.gz.sha256`)
for `URL_HASH`.

`add_subdirectory(client-monitor-cpp)` on a git submodule works the same way. As a dependency the
library builds neither its tests nor its examples.

Next to libmediasoupclient, add libmediasoupclient first and turn the binding on. That is all:

```cmake
set(LIBWEBRTC_INCLUDE_PATH /path/to/webrtc/include)   # libmediasoupclient needs these anyway
set(LIBWEBRTC_BINARY_PATH  /path/to/webrtc/lib)
add_subdirectory(libmediasoupclient)                  # defines the mediasoupclient target

set(CLIENTMONITOR_MEDIASOUPCLIENT ON)
FetchContent_MakeAvailable(clientmonitor)

target_link_libraries(my_app PRIVATE clientmonitor::clientmonitor mediasoupclient)
```

The binding then compiles against libmediasoupclient's own nlohmann::json (libsdptransform's
`json.hpp`, found automatically; two copies in one program would be an ODR violation), and picks
up libwebrtc's headers from `LIBWEBRTC_INCLUDE_PATH` as system headers, so an application building
with `-Werror` does not trip over libwebrtc's warnings. libwebrtc's headers need clang, as
libwebrtc itself does. CI compiles both bindings and
[`examples/mediasoup_client.cpp`](examples/mediasoup_client.cpp), a complete integration, against
libmediasoupclient 3.5.0 and its `v3` head with libwebrtc m140 (`test/bindings`).

### Options

| Option | Default | What it does |
|---|---|---|
| `CLIENTMONITOR_BUILD_TESTS` | ON when top level | Catch2 unit tests |
| `CLIENTMONITOR_BUILD_EXAMPLES` | ON when top level | `examples/basic_monitor` |
| `CLIENTMONITOR_LIBWEBRTC` | OFF | `bindings/libwebrtc` (needs `LIBWEBRTC_INCLUDE_PATH`, optionally `LIBWEBRTC_BINARY_PATH`) |
| `CLIENTMONITOR_MEDIASOUPCLIENT` | OFF | `bindings/mediasoupclient` (needs the `mediasoupclient` target; implies the libwebrtc binding) |
| `CLIENTMONITOR_JSON_INCLUDE_PATH` | "" | A directory with a flat `json.hpp` to use instead of nlohmann_json. With the libmediasoupclient binding it defaults to libsdptransform's |
| `CLIENTMONITOR_FETCH_JSON` | ON | Fetch nlohmann_json 3.11.3 when no `nlohmann_json::nlohmann_json` target or package exists |
| `CLIENTMONITOR_LOG_TRACE` | OFF | `CM_TRACE()` logging |

nlohmann::json is picked in this order: `CLIENTMONITOR_JSON_INCLUDE_PATH` (libsdptransform's when
the libmediasoupclient binding is on), an existing `nlohmann_json::nlohmann_json` target,
`find_package(nlohmann_json)`, then a download.

## Android (Gradle)

Gradle does not build C++ itself: it hands native code to CMake through `externalNativeBuild`, so
the library goes into the app's native `CMakeLists.txt` as above.

**1. Point Gradle at the native CMake project** (`app/build.gradle.kts`):

```kotlin
android {
    ndkVersion = "26.1.10909125"          // any recent NDK

    defaultConfig {
        minSdk = 24
        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17"
                arguments += listOf("-DANDROID_STL=c++_shared")
            }
        }
        ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64") }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"            // the SDK's bundled CMake is enough (3.14+ needed)
        }
    }
}
```

**2. Pull the library in** (`app/src/main/cpp/CMakeLists.txt`):

```cmake
cmake_minimum_required(VERSION 3.14)
project(myapp_native LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(
    clientmonitor
    URL https://github.com/ObserveRTC/client-monitor-cpp/releases/download/v0.3.0/client-monitor-cpp.tar.gz
    # or always the newest release (resolved on the first sync of a build directory only):
    # URL https://github.com/ObserveRTC/client-monitor-cpp/releases/latest/download/client-monitor-cpp.tar.gz
)
FetchContent_MakeAvailable(clientmonitor)

add_library(myapp_native SHARED native-lib.cpp)
target_link_libraries(myapp_native PRIVATE clientmonitor::clientmonitor log)
```

The release archive `URL` needs no git on the build machine and pins the version; see "Add it to
your application" for the trade-offs of `latest`. With libmediasoupclient, add it before this
block and set `CLIENTMONITOR_MEDIASOUPCLIENT` and `CLIENTMONITOR_JSON_INCLUDE_PATH` as shown above.

**3. Android specifics**

- **Network at configure time.** FetchContent downloads this library and nlohmann::json on the
  first Gradle sync. For offline or locked-down CI, add the repository as a git submodule and use
  `add_subdirectory(third_party/client-monitor-cpp)`, or pass
  `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` once the sources are cached.
- **One C++ runtime.** Build every native library with the same STL (`c++_shared`), particularly
  next to a prebuilt libwebrtc; mixing static and shared runtimes across libraries crashes in ways
  that are hard to trace.
- **libwebrtc binding.** Compile it against the same libwebrtc headers your Android WebRTC build
  uses: set `LIBWEBRTC_INCLUDE_PATH` (and `LIBWEBRTC_BINARY_PATH` if you link libwebrtc here).
- **Threads.** The internal timer thread works on Android. The integration notes below apply
  unchanged: never call `Collect()` on the signaling thread or the UI thread.
- **Logging to logcat.**

  ```cpp
  #include <android/log.h>
  #include <clientmonitor/Logger.hpp>

  class LogcatHandler : public observertc::clientmonitor::Logger::LogHandlerInterface
  {
  	void OnLog(observertc::clientmonitor::Logger::LogLevel /*level*/, char* payload, size_t /*len*/) override
  	{
  		__android_log_print(ANDROID_LOG_INFO, "ClientMonitor", "%s", payload);
  	}
  };

  static LogcatHandler logcatHandler;

  observertc::clientmonitor::Logger::SetHandler(&logcatHandler);
  observertc::clientmonitor::Logger::SetLogLevel(observertc::clientmonitor::Logger::LogLevel::LOG_WARN);
  ```

**4. A Kotlin or Java app**

The monitor runs in the native layer; Kotlin needs only a thin JNI bridge:

- create the `ClientMonitor` in a JNI `init` call and `Close()` / destroy it in a `dispose` call,
  from a thread other than the timer's;
- implement a `ClientMonitor::Listener` whose `OnSampleCreated` copies `sample.ToJson().dump()` into
  a queue, and deliver the strings to Kotlin from your own thread (attach it with
  `JavaVM::AttachCurrentThread`), never from inside the callback;
- expose the few calls the app makes itself, such as `SetInboundTrackContext()`, `AddEvent()` or
  `RaiseIssue()`, as JNI methods.

If Kotlin owns the WebRTC stack (the Android WebRTC SDK's `PeerConnection`), a Kotlin monitor fits
better; this library fits when the WebRTC stack is native, for example libmediasoupclient behind JNI.

## Build and test this repository

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/examples/basic_monitor
```

`test/consumer` is a minimal application that pulls the library in through FetchContent; CI builds
it against the checked-out sources to keep the instructions above honest.

## Use

```cpp
class MyListener : public observertc::clientmonitor::ClientMonitor::Listener
{
public:
	void OnSampleCreated(observertc::clientmonitor::ClientMonitor*, const observertc::clientmonitor::ClientSample& sample) override
	{
		observer.Send(sample.ToJson().dump());
	}

	void OnIssue(observertc::clientmonitor::ClientMonitor*, const observertc::clientmonitor::ReportedClientIssue& issue) override
	{
		// "dry-inbound-track", "transport-delay-degraded", "ice-disconnected", your own...
	}
};

observertc::clientmonitor::ClientMonitorConfig config;

config.clientId = "client-1";
config.callId   = "call-1";

MyListener listener;
observertc::clientmonitor::ClientMonitor monitor(config, &listener); // collects every 5s on its own thread

// libmediasoupclient: decorate the transport listener, bind, report producers/consumers.
// The samples then carry the transport id, direction, producer / consumer / data producer ids
// as attachments; see docs/USAGE.md#mediasoup-attachments and examples/mediasoup_client.cpp.
observertc::clientmonitor::mediasoupclient::TransportMonitor transportMonitor(&monitor);
observertc::clientmonitor::mediasoupclient::MonitoredSendTransportListener sendListener(&transportMonitor, &myListener);
auto* sendTransport = device.CreateSendTransport(&sendListener, ...);
transportMonitor.Bind(sendTransport);
auto* producer = sendTransport->Produce(...);
transportMonitor.OnProducerCreated(producer);

// Plain libwebrtc: decorate the PeerConnectionObserver, then bind.
observertc::clientmonitor::libwebrtc::MonitoredPeerConnectionObserver observer(&monitor, &myObserver);
auto pc = factory->CreatePeerConnectionOrError(config, webrtc::PeerConnectionDependencies(&observer)).MoveValue();
observer.Bind(pc);

// Anything else: a StatsCollector returning the W3C report as a JSON array.
monitor.AddPeerConnection(std::make_shared<observertc::clientmonitor::FunctionStatsCollector>([] { return GetMyStats(); }));
```

**[docs/USAGE.md](docs/USAGE.md)** has a recipe for every API: `AddEvent`, `AddMetaData`,
`AddIssue`, `RaiseIssue` / `ResolveIssue`, `AddExtensionStats` and providers, track context,
reading metrics and verdicts, detectors, score, logging and `Close()`.
[`examples/api_tour.cpp`](examples/api_tour.cpp) runs all of it.

## Integration notes

Read these before wiring the monitor into an application. Most of them come from libwebrtc's
threading model rather than from this library.

### Threads and `Collect()`

- **Never collect on the thread that delivers stats.** libwebrtc answers `GetStats()` on its
  signaling thread, and both the libwebrtc binding and libmediasoupclient's `Transport::GetStats()`
  block until it does. Calling `Collect()` there waits for itself forever. In an app where the
  signaling thread is the main thread, that means: do not call `Collect()` from the main thread.
- **Prefer the internal timer** (`collectingPeriodInMs > 0`, 5000 by default). It collects on its
  own thread, which is never the signaling thread. Set `collectingPeriodInMs = 0` only to drive
  `Collect()` from a worker thread you own, at your own cadence (and set
  `statsGapDetector->expectedPeriodInMs` to that cadence).
- **Keep collection off a UI thread anyway.** A collection waits for libwebrtc and can take tens of
  milliseconds on a busy client.
- `StatsCollector::GetStats()` runs **without** the monitor's lock, so the signaling thread can call
  into the monitor (track added, state changed) while a collection waits on it.

### Listener callbacks

- Callbacks run on the collecting thread, **with the monitor's lock held**. They may call back into
  the monitor (the lock is recursive), but must not block, and must not wait on another thread
  that might itself be waiting for the monitor.
- Hand samples off rather than sending them inline: copy `sample.ToJson()` into a queue that your
  network thread drains.
- Listeners are not owned. Remove a listener (`RemoveListener()`) before destroying it, or make sure
  it outlives the monitor.
- Do not destroy the `ClientMonitor` from inside one of its callbacks. Calling `Close()` from a
  callback is fine.

### Lifetimes

- Monitor pointers handed out (`PeerConnectionMonitor*`, `InboundTrackMonitor*`, `OutboundRtpMonitor*`,
  ...) stay valid until the next collection, which may drop the stats object behind them. Look them
  up again rather than storing them.
- The `ClientMonitor` destructor closes it and joins the timer thread: destroy it from a thread
  other than the timer's, before the peer connections or transports it reads from.
- libmediasoupclient: call `TransportMonitor::Close()` before closing the transport, and keep the
  transport alive for one more collecting period, since a collection may still be reading it.

### Tracks and context

- libwebrtc has no `ended` event. When a track ends, call `PeerConnectionMonitor::OnMediaTrackEnded()`
  (the bindings do this for removed receivers and closed producers and consumers).
- A mediasoup consumer can be created paused. Declare it through `SetInboundTrackContext()` with
  `context.paused = true`, before or after the track appears, or the
  dry-track detector reports a paused consumer as dry. `TransportMonitor` does this for you.
- Declare what the stats cannot show (linked audio/video tracks, presented resolution, screen share)
  through `SetInboundTrackContext()` / `SetOutboundTrackContext()`.

### Samples and events

- A `ClientSample` is created every `samplingPeriodInMs` (a multiple of `collectingPeriodInMs`).
  Ship `sample.ToJson()` to the observer.
- Client events, meta data and extension stats are **dropped**, not buffered, while neither sampling
  nor `bufferingEventsForSamples` is on. Turn one of them on before adding events.
- `bufferClientSamplesUntilSubscriber` holds samples created before the first listener is added.

### Build and link

- Next to libmediasoupclient the two libraries must share one nlohmann::json; two different copies
  in one binary is an ODR violation that may only show up as crashes. With
  `CLIENTMONITOR_MEDIASOUPCLIENT` on, libsdptransform's `json.hpp` is used automatically; set
  `CLIENTMONITOR_JSON_INCLUDE_PATH` yourself only if libmediasoupclient gets its json elsewhere.
- The libwebrtc binding must be compiled against the same libwebrtc headers, defines and standard
  library as the application's libwebrtc build.
- Nothing is logged until a handler is set: `observertc::clientmonitor::Logger::SetDefaultHandler()` (stdout)
  or `observertc::clientmonitor::Logger::SetHandler()` with your own, and `observertc::clientmonitor::Logger::SetLogLevel()`.
- Every header lives under `include/clientmonitor/` and is included with that prefix
  (`<clientmonitor/Logger.hpp>`), because libmediasoupclient puts its own `Logger.hpp`,
  `Transport.hpp`, ... on the include path unprefixed. The two loggers coexist: ours is
  `observertc::clientmonitor::Logger` with `CM_*` macros, theirs `mediasoupclient::Logger` with `MSC_*`. The
  unqualified global `Logger` alias belongs to libmediasoupclient, so always qualify ours.

## Layout

```
include/clientmonitor/ClientMonitor.hpp        ClientMonitor + Listener (the public entry point)
include/clientmonitor/ClientMonitorConfig.hpp  config and detector configs (std::nullopt disables a detector)
include/clientmonitor/monitors/                PeerConnectionMonitor, RTP / ICE / media / track monitors
include/clientmonitor/detectors/               Detector, Detectors, the ported detectors
include/clientmonitor/schema/                  ClientSample.hpp, Samples.hpp, JsonSupport.hpp: GENERATED by observertc-schemas
                                               SchemaTypes.hpp: brings those types into clientmonitor
include/clientmonitor/utils/                   SlicedWindow, IssueRegistry, IntervalTimer, ...
include/clientmonitor/libwebrtc/               libwebrtc binding headers
include/clientmonitor/mediasoupclient/         libmediasoupclient binding headers
bindings/                                      the bindings' sources, built only when enabled
cmake/version.hpp.in                           <clientmonitor/version.hpp>
test/                                          Catch2 tests, run without libwebrtc
test/consumer/                                 an application consuming the library through FetchContent
examples/                                      basic_monitor, and api_tour (the snippets of docs/USAGE.md)
docs/USAGE.md                                  the API guide
```

The sample types come from the observertc-schemas project: `npm run generate:cpp` there writes
`outputs/cpp/ClientSample.hpp`, `Samples.hpp` and `JsonSupport.hpp`, which are copied unchanged into
`include/clientmonitor/schema/`. When the schema moves, regenerate there and copy the three files over. They live
in `observertc::schemas`; `include/clientmonitor/schema/SchemaTypes.hpp` brings the client sample types into
`observertc::clientmonitor`. Optional lists in a sample are `std::optional<std::vector<...>>`; the monitor always
sets the ones it samples, so they appear in the JSON even when empty, as in the JS library.

See [PORTING.md](PORTING.md) for what is ported, what is not yet, and where the C++ differs.

## Conventions

The code follows the conventions of [libmediasoupclient](https://github.com/versatica/libmediasoupclient),
the native C++ mediasoup client:

- everything lives in `observertc::clientmonitor`, next to the generated `observertc::schemas`
  (an application can shorten it with `namespace cm = observertc::clientmonitor;`);
- one class per concept, `PascalCase` methods (`GetId()`, `Close()`), `camelCase` members, and
  `this->` on every member access;
- events through nested abstract `Listener` classes (`ClientMonitor::Listener`), not an event emitter;
- `nlohmann::json` for free-form data such as payloads, attachments and raw stats;
- a `Logger` with per-file `CM_CLASS` macros (`CM_DEBUG`, `CM_WARN`, ...), and the same
  `.clang-format`.

These are conventions the maintainer has followed in the ObserveRTC TypeScript libraries for years
and finds useful: the JS and C++ code read alike, which keeps a port reviewable side by side, and an
application already built on libmediasoupclient finds the API familiar. New code should keep to them.

## Releasing

1. Bump `VERSION` in `project(...)` in `CMakeLists.txt` and add a `## X.Y.Z` section to
   `CHANGELOG.md`.
2. Commit, then tag and push: `git tag -a v0.3.0 -m "client-monitor-cpp 0.3.0" && git push origin main v0.3.0`.
3. The release workflow checks the tag against `VERSION`, builds and tests, and publishes a GitHub
   release with `client-monitor-cpp.tar.gz` (and its `.sha256`), using the CHANGELOG section as
   the notes. It fails, and publishes nothing, if the versions differ or the section is missing.
4. Applications pinned to a version move by changing the tag in their URL; those on
   `releases/latest` pick it up on their next clean configure.

## License

Apache-2.0, as client-monitor-js. See [LICENSE](LICENSE).
