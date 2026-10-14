# Using client-monitor-cpp

Recipes for the public API of `observertc::clientmonitor::ClientMonitor`. The snippets come from
[`examples/api_tour.cpp`](../examples/api_tour.cpp), which CI compiles and runs, so keep the two in
sync.

All methods are thread-safe (one recursive mutex per monitor). Read the
[integration notes](../README.md#integration-notes) before wiring it into an app: above all, never
call `Collect()` on the WebRTC signaling thread.

```cpp
#include <clientmonitor/ClientMonitor.hpp>
#include <clientmonitor/schema/ClientEventTypes.hpp> // ClientEventTypes::*, ClientMetaTypes::*

using json = nlohmann::json;
```

Everything is in `observertc::clientmonitor`. The snippets spell it out; in your code
`namespace cm = observertc::clientmonitor;` keeps it short.

- [Create a monitor](#create-a-monitor)
- [One monitor, several calls](#one-monitor-several-calls)
- [Listen](#listen)
- [Client events: `AddEvent`](#client-events-addevent)
- [Meta data: `AddMetaData`](#meta-data-addmetadata)
- [Issues: `AddIssue`, `RaiseIssue`, `ResolveIssue`](#issues)
- [Extension stats: `AddExtensionStats` and providers](#extension-stats)
- [Peer connections and tracks](#peer-connections-and-tracks)
- [Attachments](#attachments), and [what the mediasoup binding attaches](#mediasoup-attachments)
- [Track context](#track-context)
- [Collect, sample and read state back](#collect-sample-and-read-state-back)
- [Detectors](#detectors)
- [Score](#score)
- [Logging](#logging)
- [Close](#close)
- [What ends up in a sample](#what-ends-up-in-a-sample)

## Create a monitor

```cpp
observertc::clientmonitor::ClientMonitorConfig config;

config.clientId             = "client-1"; // optional, both go into every sample
config.callId               = "call-1";
config.collectingPeriodInMs = 5000;       // the internal timer thread collects; 0 = call Collect() yourself
config.samplingPeriodInMs   = 5000;       // a ClientSample every 5s; 0 = no samples

AppListener listener;
observertc::clientmonitor::ClientMonitor monitor(config, &listener); // the listener may be nullptr

// Free-form data copied into every sample.
monitor.SetAttachments({ { "roomId", "room-42" }, { "userId", "user-7" } });
```

Leaving a detector's config as `std::nullopt` leaves the detector out entirely:

```cpp
config.transportDelayDetector = std::nullopt;
```

Listeners can be added and removed later with `AddListener()` / `RemoveListener()`.

## One monitor, several calls

A client that keeps one monitor for its lifetime, as the web client does, samples only while it
is in a call and keeps what happens before (device snapshots, capture failures) for the first
sample of the call:

```cpp
config.collectingPeriodInMs         = 5000;
config.samplingPeriodInMs           = 0;     // nothing is sampled before the call
config.bufferingEventsForSamples    = true;  // ... but events, issues and meta are kept for it
config.addClientJointEventOnCreated = false; // CLIENT_JOINED / CLIENT_LEFT per call, not per monitor
config.addClientLeftEventOnClose    = false;

// The call is up.
monitor.SetCallId(callId);
monitor.AddClientJoinEvent();
monitor.SetSamplingPeriod(5000); // the next sample carries everything buffered so far

// The call ends (payload { "reason": "livecall-rejoin" } when it is replaced by a rejoin).
monitor.AddClientLeftEvent();
monitor.CreateSample();
monitor.SetSamplingPeriod(0);
```

## Listen

Derive from `ClientMonitor::Listener` and override what you need; every method has a no-op default.
Callbacks run on the collecting thread (or the thread that called the API that triggered them),
with the monitor's lock held: keep them short and never block on the signaling thread there.

```cpp
class AppListener : public observertc::clientmonitor::ClientMonitor::Listener
{
public:
	void OnSampleCreated(observertc::clientmonitor::ClientMonitor*, const observertc::clientmonitor::ClientSample& sample) override
	{
		// Queue it for your network thread; do not send it from here.
		queue.Push(sample.ToJson().dump());
	}

	void OnIssue(observertc::clientmonitor::ClientMonitor*, const observertc::clientmonitor::ReportedClientIssue& issue) override
	{
		// issue.resolvable: true for RaiseIssue(), false for AddIssue(). issue.key is empty for the latter.
	}

	void OnIssueUpdated(observertc::clientmonitor::ClientMonitor*, const observertc::clientmonitor::RaisedClientIssue& issue) override {}

	void OnIssueResolved(observertc::clientmonitor::ClientMonitor*, const observertc::clientmonitor::ResolvedClientIssue& issue) override
	{
		// issue.key, issue.comment, issue.resolvedAt - issue.raisedAt
	}

	void OnClientEvent(observertc::clientmonitor::ClientMonitor*, const observertc::clientmonitor::ClientEvent& event) override {}
};
```

Other callbacks: `OnStatsCollected`, `OnScore`, `OnMeta`, `OnExtensionStats`,
`OnStatsCollectionGap`, `OnDryInboundTrack`, `OnDryOutboundTrack`, `OnTransportDelayDegraded`,
`OnClose`, and `OnNew*Monitor` for every stats object the monitor starts tracking.

## Client events: `AddEvent`

```cpp
void AddEvent(const std::string& type, const json& payload = json(), std::optional<int64_t> timestamp = std::nullopt);
```

An event is reported to `OnClientEvent()` and buffered into the next sample. The timestamp is epoch
milliseconds and defaults to now.

```cpp
// Your own event type.
monitor.AddEvent("USER_RAISED_HAND", { { "userId", "user-7" } });

// One of the library's types, with an explicit timestamp.
monitor.AddEvent(
  observertc::clientmonitor::ClientEventTypes::MEDIA_TRACK_MUTED,
  { { "trackId", "local-audio" }, { "reason", "user" } },
  1700000000000);
```

`CLIENT_JOINED` is added on construction and `CLIENT_LEFT` on `Close()` (turn them off with
`addClientJointEventOnCreated` / `addClientLeftEventOnClose`). To add them yourself, for example
when the user joins later than the monitor is created, use `AddClientJoinEvent(payload, timestamp)`
and `AddClientLeftEvent(payload, timestamp)`.

The bindings add the peer connection, track, ICE and mediasoup producer/consumer events
themselves. `ClientEventTypes` lists them all.

## Meta data: `AddMetaData`

Facts about the client rather than moments in the call. Same shape as `AddEvent`, reported to
`OnMeta()`.

```cpp
monitor.AddMetaData(
  observertc::clientmonitor::ClientMetaTypes::OPERATION_SYSTEM, { { "name", "macOS" }, { "version", "15.1" } });
```

An object payload's `peerConnectionId` and `trackId` are copied to the item's own fields. When the
payload is not an object, give the fields directly:

```cpp
observertc::clientmonitor::ClientMetaData sdp;

sdp.type             = observertc::clientmonitor::ClientMetaTypes::LOCAL_SDP;
sdp.payload          = "v=0\r\n...";
sdp.peerConnectionId = "pc-1";
monitor.AddMetaData(sdp);
```

## Issues

Two kinds:

| | `AddIssue` | `RaiseIssue` / `ResolveIssue` |
|---|---|---|
| Models | a moment ("the user rated audio 2/5") | a condition with a lifetime ("signalling is down") |
| Identity | none | `key`, unique within the monitor |
| Listener | `OnIssue` | `OnIssue`, then `OnIssueUpdated` on re-raise, `OnIssueResolved` |
| In the sample | one entry | one entry on raise, and a `<type>-resolved` entry on resolve (`sendResolvedIssuesToServer`) |

The library's own detectors use `RaiseIssue` too (`dry-inbound-track`, `transport-delay-degraded`,
`ice-disconnected`, ...), so your listener sees both.

### `AddIssue`

```cpp
std::optional<AddedClientIssue> AddIssue(
  const std::string& type, const json& payload = json(),
  std::optional<int64_t> timestamp = std::nullopt, bool includeInSample = true);
```

```cpp
monitor.AddIssue("user-reported-bad-audio", { { "rating", 2 }, { "comment", "robotic voice" } });

// Reported to listeners only, kept out of the sample.
monitor.AddIssue("local-diagnostic", { { "detail", "x" } }, std::nullopt, /*includeInSample*/ false);
```

### `RaiseIssue`

```cpp
const RaisedClientIssue* RaiseIssue(
  const std::string& key, const std::string& type, const json& payload = json(),
  std::optional<int64_t> timestamp = std::nullopt, bool includeInSample = true);
```

```cpp
monitor.RaiseIssue("signalling-down", "signalling-unreachable", { { "attempts", 1 } });

// Same key again: updated in place (payload replaced, updatedAt bumped, OnIssueUpdated), not duplicated.
monitor.RaiseIssue("signalling-down", "signalling-unreachable", { { "attempts", 3 } });
```

Pick keys that name the condition and its subject, e.g. `"signalling-down"` or
`"camera-blocked:" + deviceId`, so the code that clears it can build the same key.

### Query open issues

```cpp
if (monitor.IsIssueActive("signalling-down"))
{
	for (const auto& issue : monitor.GetActiveIssuesByType("signalling-unreachable"))
	{
		// issue.key, issue.raisedAt, issue.updatedAt, issue.payload
	}
}

// Everything open, the library's and yours: monitor.GetActiveIssues() (an IssueRegistry).
```

### `ResolveIssue`

```cpp
std::optional<ResolvedClientIssue> ResolveIssue(
  const std::string& key, const std::optional<std::string>& comment = std::nullopt,
  const json& payload = json(), std::optional<int64_t> resolvedAt = std::nullopt);
```

```cpp
auto resolved = monitor.ResolveIssue("signalling-down", std::string("reconnected"), { { "reconnectInMs", 1800 } });

// std::nullopt when the key was not active, so resolving twice is harmless.
```

`Close()` resolves whatever is still open with the comment
`"monitor closed before issue could be resolved"`.

## Extension stats

Application measurements that are not WebRTC stats (rendering, CPU, a custom jitter buffer, ...).
They ship in the sample's `extensionStats` and are reported to `OnExtensionStats()`.

```cpp
struct ClientMonitor::ExtensionStatsInput
{
	std::string type;
	json payload;
	std::optional<std::string> id; // set it to read the payload back
};
```

### `AddExtensionStats`

```cpp
// Without an id: goes into the next sample, nothing is kept.
monitor.AddExtensionStats({ "render-stats", { { "canvasFps", 24 } }, std::nullopt });

// With an id: also readable back, until one collection after it stops being reported.
monitor.AddExtensionStats({ "render-stats", { { "droppedFrames", 3 }, { "canvasFps", 24 } }, std::string("tile-42") });

if (auto payload = monitor.GetExtensionStatsPayload("tile-42"))
{
	int dropped = (*payload)["droppedFrames"].get<int>();
}

// GetExtensionStatsEntry(id) also gives the type and timestamp.
```

### Providers

A provider runs on every collection, on the collecting thread, so its value is always fresh.
Return `std::nullopt` to report nothing this time.

```cpp
const auto handle = monitor.AddExtensionStatsProvider(
  [&cpu]() -> std::optional<observertc::clientmonitor::ClientMonitor::ExtensionStatsInput>
  {
	  return observertc::clientmonitor::ClientMonitor::ExtensionStatsInput{ "system-metrics", { { "cpu", cpu.Read() } }, std::string("system") };
  });

// later
monitor.RemoveExtensionStatsProvider(handle);
```

Whatever the lambda captures must outlive the provider: remove it before those objects go away.

## Peer connections and tracks

With libwebrtc or libmediasoupclient, use the bindings (see the README's "Use" section): they add
the peer connection, the tracks and the events for you.

Anything else feeds a `StatsCollector`. `FunctionStatsCollector` wraps a callable that returns the
W3C stats report as a JSON array:

```cpp
auto collector = std::make_shared<observertc::clientmonitor::FunctionStatsCollector>([&]() { return GetMyStatsAsJson(); });

auto* peerConnection = monitor.AddPeerConnection(collector, "pc-1", { { "role", "recv" } }); // id and attachments optional

peerConnection->AddMediaTrack(
  std::make_shared<observertc::clientmonitor::BasicMediaTrack>("remote-audio", "audio"), { { "participant", "alice" } });

// When done: monitor.RemovePeerConnection("pc-1");
```

The track id must match the stats' `trackIdentifier`. Track attachments are copied into that
track's sample entries.

## Attachments

Attachments are free-form JSON shipped on an object's entry in every sample. The observer joins on
them, for example a client's outbound track to the SFU's producer. They can go on every level:

| Level | Set with | Ends up in |
|---|---|---|
| Client | `monitor.SetAttachments(json)` replaces, `monitor.UpdateAttachments(patch)` merges | `ClientSample.attachments` |
| Peer connection | `AddPeerConnection(collector, id, attachments)`, or `GetAttachments()` on its monitor | `peerConnections[].attachments` |
| Track | `AddMediaTrack(track, attachments)`, or `GetAttachments()` on the track monitor | `inboundTracks[]` / `outboundTracks[].attachments` |
| Data channel | `peerConnection->SetDataChannelAttachments(sctpStreamId, json)` | `dataChannels[].attachments` |
| Any other stats object | `UpdateAttachments(patch)` on its monitor (`GetOutboundRtpMonitor(ssrc)`, ...) | that entry's `attachments` |

`UpdateAttachments` is an RFC 7386 merge patch: keys given are set, keys set to `null` removed,
the rest kept. That suits attachments owned by different parts of an application:

```cpp
monitor.UpdateAttachments({
  { "senderSideCoupledCongestion", { { "enabled", true }, { "enforcedPolicy", "v1" } } }
});
monitor.UpdateAttachments({ { "senderSideCoupledCongestion", nullptr } }); // gone again
```

Data channel attachments are matched by `dataChannelIdentifier`, the SCTP stream id, and can be
declared before the channel's stats exist. Stats monitors keep their attachments across reports;
they are lost only when the stats object goes away.

Like every monitor pointer, the per-object `GetAttachments()` / `UpdateAttachments()` take no lock:
call them inside a listener callback, or under `std::lock_guard lock(monitor.GetMutex())`.
`ClientMonitor::UpdateAttachments()` and `SetDataChannelAttachments()` lock themselves.

### Mediasoup attachments

With `CLIENTMONITOR_MEDIASOUPCLIENT`, `mediasoupclient::TransportMonitor` adds what the web
client's monitor adds when it binds a mediasoup device, so the samples of a native client and a
web client join to the SFU the same way:

| Sample field | Value | Set by |
|---|---|---|
| `peerConnections[].peerConnectionId` | the transport id | `Bind()` |
| `peerConnections[].attachments.direction` | `"send"` or `"recv"` | `Bind()` |
| `outboundTracks[].attachments` | `{ producerId }`, kept across `ReplaceTrack()` | `OnProducerCreated()`, `OnProducerTrackReplaced()` |
| `inboundTracks[].attachments` | `{ producerId, consumerId }` | `OnConsumerCreated()` |
| `dataChannels[].attachments` | `{ dataProducerId }` or `{ dataProducerId, dataConsumerId }` | `OnDataProducerCreated()`, `OnDataConsumerCreated()` |

and the events `PEER_CONNECTION_OPENED` / `_CLOSED` / `_STATE_CHANGED`, `PRODUCER_ADDED` /
`_REMOVED` / `_PAUSED` / `_RESUMED`, `CONSUMER_*`, `DATA_PRODUCER_*`, `DATA_CONSUMER_*` and
`MEDIA_TRACK_REMOVED` with the producer and consumer ids spread in, each with its
`peerConnectionId`. It also keeps the track contexts (`paused`, and `remoteOutboundTrackPaused`
through `OnConsumerProducerPaused()` / `OnConsumerProducerResumed()`) that stop detectors from
reporting a paused track as dry.

libmediasoupclient has no observer to subscribe to, so the application calls the `On*` methods
where it creates, pauses and closes things. [`examples/mediasoup_client.cpp`](../examples/mediasoup_client.cpp)
does all of it in one class; CI compiles it against libmediasoupclient.

Not available from libmediasoupclient, so not sent by the binding: `ICE_GATHERING_STATE_CHANGED`,
`MEDIA_TRACK_MUTED` / `_UNMUTED` and the track `label` / `contentHint` in track events.

## Track context

What the stats cannot show: a track paused on purpose, shown at a given size, linked to a video
track. Detectors use it, for example a paused track is not reported as dry. It can be set before
the track exists; it is applied when the track appears.

```cpp
observertc::clientmonitor::InboundTrackContext context;

context.paused = true; // e.g. a mediasoup consumer created paused
monitor.SetInboundTrackContext("remote-audio", context);

context.paused = false; // resumed later
monitor.SetInboundTrackContext("remote-audio", context);
```

Fields left `std::nullopt` keep their previous value. `InboundTrackContext` also has
`contentType`, `linkedVideoTrackId`, `motionType`, `presentedResolution`,
`remoteOutboundTrackPaused` and `dtxMode`; `OutboundTrackContext` has `contentType` and `paused`
(`SetOutboundTrackContext`).

## Collect, sample and read state back

```cpp
monitor.Collect(); // only when collectingPeriodInMs == 0; never on the signaling thread

double bitrate = peerConnection->GetMetrics().receivingAudioBitrate;

if (auto* track = monitor.GetInboundTrackMonitor("remote-audio"))
{
	bool dry = track->GetVerdicts().dry.value_or(false);
}

if (auto sample = monitor.CreateSample()) // on demand; otherwise every samplingPeriodInMs
{
	std::string body = sample->ToJson().dump();
}
```

Every monitor has `GetStats()` (the raw report), `GetMetrics()` (derived values such as bitrates
and loss) and `GetVerdicts()` (detector outcomes). Monitor pointers stay valid until the next
collection; do not keep them across collections. `SetCollectingPeriod()` and
`SetSamplingPeriod()` change the periods at runtime.

## Detectors

```cpp
peerConnection->GetDetectors().Disable("transport-delay-detector");
peerConnection->GetDetectors().Enable("transport-delay-detector");

// The client-level ones: monitor.GetDetectors(). Also Has(), GetNames(), DisableAll(), Add(), Remove().
```

To write your own, see "Porting a detector" in [AGENTS.md](../AGENTS.md).

## Score

```cpp
monitor.SetScore(3.5, { { "audio-dry", 1.5 } }); // or install a calculator:
monitor.SetScoreCalculator(std::make_unique<MyScoreCalculator>());
double score = monitor.GetScore();
```

## Logging

```cpp
observertc::clientmonitor::Logger::SetDefaultHandler(); // stdout; or SetHandler(myHandler)
observertc::clientmonitor::Logger::SetLogLevel(observertc::clientmonitor::Logger::LogLevel::LOG_WARN);
```

This is `observertc::clientmonitor::Logger`, separate from libmediasoupclient's `mediasoupclient::Logger`;
set up both if you want both.

## Close

```cpp
monitor.Close(); // idempotent; the destructor calls it too
```

It stops the timer, resolves open issues, adds `CLIENT_LEFT`, creates a last sample when sampling,
and reports `OnClose()`. After it, the `Add*` and `Raise*` calls are ignored.

## What ends up in a sample

Events, meta data and extension stats are kept for the next sample only while sampling is on
(`samplingPeriodInMs > 0`) or `bufferingEventsForSamples` is true:

| | sampling on, or `bufferingEventsForSamples` | neither |
|---|---|---|
| `AddEvent`, `AddMetaData` | listener + sample | **dropped**, the listener is not called either |
| `AddIssue`, `RaiseIssue`, `ResolveIssue` | listener + sample | listener only |
| `AddExtensionStats` | listener + sample, readable by id | readable by id only |

So an app that only wants listener callbacks and builds samples itself with `CreateSample()`
sets `samplingPeriodInMs = 0` and `bufferingEventsForSamples = true`.

With `bufferClientSamplesUntilSubscriber`, samples created before the first listener is added are
held and replayed to it.
