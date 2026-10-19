# Changelog

All notable changes to client-monitor-cpp. Versions follow [Semantic Versioning](https://semver.org/);
the version lives in `project(... VERSION ...)` in `CMakeLists.txt` and is tagged as `vX.Y.Z`.

## 0.3.0

Outbound tracks bind to the streams their sender sends now, also when libwebrtc keeps reporting
senders that lost their track, and libwebrtc timestamps are read in milliseconds. Stats adapters
move to the client monitor.

**Breaking:** stats adapters belong to the client monitor. `PeerConnectionMonitor::AddStatsAdapter()`
/ `RemoveStatsAdapter()` and `PeerConnectionMonitor::Collect()` are gone; use
`ClientMonitor::AddStatsAdapter()` / `RemoveStatsAdapter()` / `GetStatsAdapter()`, and
`ClientMonitor::Collect()`. `StatsAdapter::Adapt()` takes the peer connection id:
`Adapt(peerConnectionId, stats)`.

- `ClientMonitor::Collect()` takes every peer connection's raw report, reports it through the new
  `Listener::OnRawStats()`, runs the adapters, then hands the adapted report to the peer connection
  monitor. `PeerConnectionMonitor::Accept()` takes the same path; `GetRawStats()` returns the
  collector's report. The client monitor starts with a `LibWebRtcStatsAdapter`.
- `LibWebRtcStatsAdapter` converts microsecond timestamps (libwebrtc's `RTCStatsReport::ToJson()`)
  to milliseconds. Before, every elapsed time read 1000 times too long: rates were 1000 times too
  low, and millisecond thresholds such as the dry detectors' were crossed after one collection. The
  timestamps in the samples are milliseconds now too (see "Sample timestamps" below).
- `DetachedSenderStatsAdapter`: removes `mediaSourceId` from the `outbound-rtp` entries of senders
  that carry no track, as W3C stats have it; libwebrtc keeps reporting such senders with a stale
  reference. Told per peer connection and by `mid`; the entries stay in the report.
- The mediasoupclient binding adds it to the client monitor for send transports and tells it, by
  transport id and `mid`, which producers' senders carry a track (created, track replaced, closed).
- Outbound tracks re-read their `outbound-rtp` streams from `mediaSourceId` on every report, also
  when a report repeats its timestamp, and take the media source a stream references when several
  carry the track's id. `dry-outbound-track` is no longer raised for a sending track bound to a
  closed sender's streams.
- A new outbound track with the id of an ended one starts a new monitor, instead of being ignored
  while the ended one's streams, attachments and detector state stay.

### Sample timestamps

**Changed for sample consumers:** the `timestamp` of every stats object inside
`peerConnections[]` (`outboundRtps`, `inboundRtps`, `remoteInboundRtps`, `remoteOutboundRtps`,
`mediaSources`, `mediaPlayouts`, `codecs`, `dataChannels`, `peerConnectionTransports`,
`iceTransports`, `iceCandidates`, `iceCandidatePairs`, `certificates`) is now in milliseconds since
the epoch, as in samples from a browser.

- Up to 0.2.0 these were microseconds, as libwebrtc reports them: an `outbound-rtp` at
  `1792044664814755` next to a sample `timestamp` of `1792044676463`. From 0.3.0 the same entry
  reads `1792044664814.755`.
- Unchanged, and always milliseconds: the sample-level `timestamp`, `clientEvents[].timestamp`,
  `clientIssues[].timestamp` and `clientMetaItems[].timestamp`.
- Applications on 0.2.0 or earlier stay in use for a while, so a consumer receives both units at
  once. The unit can be told from the value itself: an epoch timestamp above `1e14` cannot be
  milliseconds (that is the year 5138), so it is microseconds and divides by 1000; anything lower
  is milliseconds. Reading the unit per value keeps both working, and does not depend on the
  client's platform or library version.
- Everything derived from stats timestamps is affected: elapsed times and the rates computed from
  them (bitrate, packet rate, frame rate), time axes, gap detection, and ordering stats against
  events and issues. A consumer that already reads microseconds for these clients should read the
  unit per value instead, so 0.3.0 samples are not read 1000 times too short.

## 0.2.0

Makes a libmediasoupclient application's samples join to the SFU the way the web client's do, and
the bindings easy to build.

**Breaking:** everything moved into the `observertc::clientmonitor` namespace (the `v0.1.0` tag
predates the move; the 0.1.0 notes below already describe it).

- Attachments on every level of a sample: `ClientMonitor::UpdateAttachments()` merges into the
  client attachments (RFC 7386, `null` removes a key); every stats monitor has `GetAttachments()` /
  `UpdateAttachments()`, kept across reports; `PeerConnectionMonitor::SetDataChannelAttachments()`
  binds a data channel's attachments by SCTP stream id.
- `ClientMonitor::AddMetaData(ClientMetaData)`, for payloads that are not objects (an SDP with its
  `peerConnectionId`). `AddMetaData({})` and `AddMetaData({ "type" })` are now ambiguous;
  write `AddMetaData("type")`.
- mediasoupclient binding, matching the web client's `MediasoupTransportBinding`:
  - `direction: "send" | "recv"` on the peer connection attachments;
  - `{ dataProducerId[, dataConsumerId] }` on the data channel entries;
  - `ReplaceTrack()` drops the old track and keeps `producerId` on the new one; a producer's track
    is remembered, so closing it works after the track is gone;
  - `MEDIA_TRACK_REMOVED` with the producer and consumer ids when either closes;
  - `OnConsumerProducerPaused()` / `OnConsumerProducerResumed()` set `remoteOutboundTrackPaused`;
  - `GetTransportId()`, `GetDirection()`.
- Build:
  - with `CLIENTMONITOR_MEDIASOUPCLIENT`, libsdptransform's `json.hpp` is found and used
    automatically. The README's earlier `${libsdptransform_SOURCE_DIR}` recipe expanded to an empty
    path from the application's scope; a `CLIENTMONITOR_JSON_INCLUDE_PATH` without a `json.hpp` is
    now an error;
  - libwebrtc's include directories are `SYSTEM`, so applications building with `-Werror` compile;
  - `test/bindings` compiles both bindings and the new `examples/mediasoup_client.cpp` against
    libmediasoupclient and a prebuilt libwebrtc m140, in CI;
  - CI ran on `master`, which does not exist; it runs on `main` now.

## 0.1.0

First release: a native C++ port of the core of
[`@observertc/client-monitor-js`](https://github.com/ObserveRTC/client-monitor-js) 4.11.0,
producing ObserveRTC `ClientSample`s on schema 3.7.0.

- `ClientMonitor` with a libmediasoupclient-style `Listener` API, its own collecting timer or
  manual `Collect()`, sampling, client events, meta data, extension stats and issues.
- `PeerConnectionMonitor` and the 15 stats monitors (RTP, ICE, codec, media source/playout,
  data channel, certificate, tracks), with the JS library's derived metrics.
- Detectors: stats gap, ICE disconnected, transport delay, dry inbound track, dry outbound track.
- `LibWebRtcStatsAdapter` (the JS `ChromeStatsAdapter`).
- Schema types generated by observertc-schemas (`npm run generate:cpp`).
- Optional bindings for libwebrtc and libmediasoupclient (not yet compiled against either).
- Everything lives in `observertc::clientmonitor`, next to the generated `observertc::schemas`.
- `docs/USAGE.md`, a recipe for every public API, and `examples/api_tour.cpp`, which compiles and
  runs them in CI.

See [PORTING.md](PORTING.md) for what is not ported yet.
