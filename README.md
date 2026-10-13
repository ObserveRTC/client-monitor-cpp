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
	GIT_TAG v0.1.0
)
FetchContent_MakeAvailable(clientmonitor)

target_link_libraries(my_app PRIVATE clientmonitor::clientmonitor)
```

```cpp
#include <clientmonitor/ClientMonitor.hpp>
```

`add_subdirectory(client-monitor-cpp)` on a git submodule works the same way. As a dependency the
library builds neither its tests nor its examples.

Next to libmediasoupclient, turn the binding on and share libmediasoupclient's json (it ships
`json.hpp` through libsdptransform), so both libraries compile against the same one:

```cmake
add_subdirectory(libmediasoupclient)                # defines the mediasoupclient target

set(CLIENTMONITOR_MEDIASOUPCLIENT ON)
set(CLIENTMONITOR_JSON_INCLUDE_PATH ${libsdptransform_SOURCE_DIR}/include)
FetchContent_MakeAvailable(clientmonitor)
```

### Options

| Option | Default | What it does |
|---|---|---|
| `CLIENTMONITOR_BUILD_TESTS` | ON when top level | Catch2 unit tests |
| `CLIENTMONITOR_BUILD_EXAMPLES` | ON when top level | `examples/basic_monitor` |
| `CLIENTMONITOR_LIBWEBRTC` | OFF | `bindings/libwebrtc` (needs `LIBWEBRTC_INCLUDE_PATH`, optionally `LIBWEBRTC_BINARY_PATH`) |
| `CLIENTMONITOR_MEDIASOUPCLIENT` | OFF | `bindings/mediasoupclient` (needs the `mediasoupclient` target; implies the libwebrtc binding) |
| `CLIENTMONITOR_JSON_INCLUDE_PATH` | "" | A directory with a flat `json.hpp` to use instead of nlohmann_json |
| `CLIENTMONITOR_FETCH_JSON` | ON | Fetch nlohmann_json 3.11.3 when no `nlohmann_json::nlohmann_json` target or package exists |
| `CLIENTMONITOR_LOG_TRACE` | OFF | `CM_TRACE()` logging |

nlohmann::json is picked in this order: `CLIENTMONITOR_JSON_INCLUDE_PATH`, an existing
`nlohmann_json::nlohmann_json` target, `find_package(nlohmann_json)`, then a download.

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
    URL https://github.com/ObserveRTC/client-monitor-cpp/archive/refs/tags/v0.1.0.tar.gz
)
FetchContent_MakeAvailable(clientmonitor)

add_library(myapp_native SHARED native-lib.cpp)
target_link_libraries(myapp_native PRIVATE clientmonitor::clientmonitor log)
```

The release archive `URL` needs no git on the build machine and pins the version. With
libmediasoupclient, add it before this block and set `CLIENTMONITOR_MEDIASOUPCLIENT` and
`CLIENTMONITOR_JSON_INCLUDE_PATH` as shown above.

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

  class LogcatHandler : public clientmonitor::Logger::LogHandlerInterface
  {
  	void OnLog(clientmonitor::Logger::LogLevel /*level*/, char* payload, size_t /*len*/) override
  	{
  		__android_log_print(ANDROID_LOG_INFO, "ClientMonitor", "%s", payload);
  	}
  };

  static LogcatHandler logcatHandler;

  clientmonitor::Logger::SetHandler(&logcatHandler);
  clientmonitor::Logger::SetLogLevel(clientmonitor::Logger::LogLevel::LOG_WARN);
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
class MyListener : public clientmonitor::ClientMonitor::Listener
{
public:
	void OnSampleCreated(clientmonitor::ClientMonitor*, const clientmonitor::ClientSample& sample) override
	{
		observer.Send(sample.ToJson().dump());
	}

	void OnIssue(clientmonitor::ClientMonitor*, const clientmonitor::ReportedClientIssue& issue) override
	{
		// "dry-inbound-track", "transport-delay-degraded", "ice-disconnected", your own...
	}
};

clientmonitor::ClientMonitorConfig config;

config.clientId = "client-1";
config.callId   = "call-1";

MyListener listener;
clientmonitor::ClientMonitor monitor(config, &listener); // collects every 5s on its own thread

// libmediasoupclient: decorate the transport listener, bind, report producers/consumers.
clientmonitor::mediasoupclient::TransportMonitor transportMonitor(&monitor);
clientmonitor::mediasoupclient::MonitoredSendTransportListener sendListener(&transportMonitor, &myListener);
auto* sendTransport = device.CreateSendTransport(&sendListener, ...);
transportMonitor.Bind(sendTransport);
auto* producer = sendTransport->Produce(...);
transportMonitor.OnProducerCreated(producer);

// Plain libwebrtc: decorate the PeerConnectionObserver, then bind.
clientmonitor::libwebrtc::MonitoredPeerConnectionObserver observer(&monitor, &myObserver);
auto pc = factory->CreatePeerConnectionOrError(config, webrtc::PeerConnectionDependencies(&observer)).MoveValue();
observer.Bind(pc);

// Anything else: a StatsCollector returning the W3C report as a JSON array.
monitor.AddPeerConnection(std::make_shared<clientmonitor::FunctionStatsCollector>([] { return GetMyStats(); }));
```

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

- Next to libmediasoupclient, set `CLIENTMONITOR_JSON_INCLUDE_PATH` to libsdptransform's include
  directory so the two libraries share one nlohmann::json. Two different copies in one binary is an
  ODR violation that may only show up as crashes.
- The libwebrtc binding must be compiled against the same libwebrtc headers, defines and standard
  library as the application's libwebrtc build.
- Nothing is logged until a handler is set: `Logger::SetDefaultHandler()` (stdout) or
  `Logger::SetHandler()` with your own, and `Logger::SetLogLevel()`.

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
```

The sample types come from the observertc-schemas project: `npm run generate:cpp` there writes
`outputs/cpp/ClientSample.hpp`, `Samples.hpp` and `JsonSupport.hpp`, which are copied unchanged into
`include/clientmonitor/schema/`. When the schema moves, regenerate there and copy the three files over. They live
in `observertc::schemas`; `include/clientmonitor/schema/SchemaTypes.hpp` brings the client sample types into
`clientmonitor`. Optional lists in a sample are `std::optional<std::vector<...>>`; the monitor always
sets the ones it samples, so they appear in the JSON even when empty, as in the JS library.

See [PORTING.md](PORTING.md) for what is ported, what is not yet, and where the C++ differs.

## Conventions

The code follows the conventions of [libmediasoupclient](https://github.com/versatica/libmediasoupclient),
the native C++ mediasoup client:

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

1. Bump `VERSION` in `project(...)` in `CMakeLists.txt` and add a section to `CHANGELOG.md`.
2. Commit, then tag and push: `git tag v0.2.0 && git push origin master v0.2.0`.
3. Applications move by changing `GIT_TAG`.

## License

Apache-2.0, as client-monitor-js. See [LICENSE](LICENSE).
