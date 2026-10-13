#ifndef CM_CLIENT_MONITOR_CONFIG_HPP
#define CM_CLIENT_MONITOR_CONFIG_HPP

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace observertc::clientmonitor
{
	/**
	 * Detector configurations. Every detector is on by default with the same defaults as the JS
	 * library; set its entry in ClientMonitorConfig to std::nullopt to disable it entirely (the JS
	 * `null`), in which case the detector is never instantiated.
	 */

	struct StatsGapDetectorConfig
	{
		// How many times the configured collecting period a gap must exceed to count.
		double gapRatioThreshold{ 2 };
		// And never below this many milliseconds.
		int64_t minGapInMs{ 5000 };
		// Whether to also add a STATS_COLLECTION_GAP client event to the sample.
		bool createEvent{ true };
		/**
		 * The period collections are expected at. Defaults to collectingPeriodInMs; set it when
		 * the application drives Collect() itself (collectingPeriodInMs = 0) at its own cadence.
		 */
		std::optional<int64_t> expectedPeriodInMs;
	};

	struct DryInboundTrackDetectorConfig
	{
		// How long a live, unpaused inbound track may receive nothing before it is dry.
		int64_t thresholdInMs{ 5000 };
	};

	struct DryOutboundTrackDetectorConfig
	{
		// How long a live, unpaused outbound track may send nothing before it is dry.
		int64_t thresholdInMs{ 5000 };
	};

	struct TransportDelayDetectorConfig
	{
		// ~300ms round trip is where turn-taking starts to break down.
		double thresholdInMs{ 300 };
		double recoveryThresholdInMs{ 200 };
	};

	struct IceDisconnectedDetectorConfig
	{
		int64_t disconnectedThresholdInMs{ 5000 };
	};

	/**
	 * A sliced window: how many values each named slice covers, and the gap that resets it.
	 * Slices are counted in collections, not milliseconds. See SlicedWindow.
	 */
	struct WindowConfig
	{
		// Defaults to four collecting periods.
		std::optional<int64_t> maxAllowedGapInMs;
		std::map<std::string, size_t> numberOfSamples;
	};

	struct ClientMonitorConfig
	{
		/**
		 * Identifies the client and the call in every sample. Optional; can be set later through
		 * ClientMonitor::SetClientId() / SetCallId().
		 */
		std::optional<std::string> clientId;
		std::optional<std::string> callId;

		/**
		 * How often the monitor collects stats on its own timer thread. 0 disables the timer: the
		 * application then drives collection by calling ClientMonitor::Collect() itself, which is
		 * the way to keep every callback on a thread it owns.
		 */
		int64_t collectingPeriodInMs{ 5000 };

		/**
		 * How often a ClientSample is created. Must be a multiple of collectingPeriodInMs; a sample
		 * is created every samplingPeriodInMs / collectingPeriodInMs collections. 0 disables
		 * automatic sampling (ClientMonitor::CreateSample() still works).
		 */
		int64_t samplingPeriodInMs{ 5000 };

		// Add a CLIENT_JOINED event when the monitor is created.
		bool addClientJointEventOnCreated{ true };
		// Add a CLIENT_LEFT event when the monitor is closed.
		bool addClientLeftEventOnClose{ true };
		/**
		 * Buffer events, issues and meta items for the sample even when sampling is off. Without
		 * it they are reported to the listeners but not kept.
		 */
		bool bufferingEventsForSamples{ false };
		// Hold created samples until the first listener is added, then replay them.
		bool bufferClientSamplesUntilSubscriber{ false };
		// Add a `<type>-resolved` entry to the sample when a raised issue resolves.
		bool sendResolvedIssuesToServer{ true };
		// Ship score reasons in the sample.
		bool sendScoreReasonsToServer{ true };
		// Static ICE transport metadata ships in a transport's first sample and on change only.
		bool sendIceTransportMetadataOnChangeOnly{ true };

		// Application data handed back untouched.
		nlohmann::json appData = nlohmann::json::object();

		// Collections per window slice. See the JS `ClientMonitorConfig` for the reasoning.
		WindowConfig outboundTrackWindow{
			std::nullopt, { { "detection", 3 }, { "recovery", 3 } }
		};
		WindowConfig inboundTrackWindow{
			std::nullopt,
			{ { "detection", 3 }, { "recovery", 3 }, { "flowDetection", 4 }, { "flowRecovery", 3 } }
		};
		WindowConfig peerConnectionWindow{
			std::nullopt, { { "detection", 3 }, { "recovery", 3 } }
		};
		WindowConfig clientWindow{
			std::nullopt, { { "detection", 3 }, { "recovery", 3 } }
		};

		/* Detectors. */

		// Telemetry: a collection that ran much later than scheduled.
		std::optional<StatsGapDetectorConfig> statsGapDetector{ StatsGapDetectorConfig{} };
		// Pipeline disruption: the receive chain delivers nothing.
		std::optional<DryInboundTrackDetectorConfig> dryInboundTrackDetector{
			DryInboundTrackDetectorConfig{}
		};
		// Pipeline disruption: the send chain puts nothing on the wire.
		std::optional<DryOutboundTrackDetectorConfig> dryOutboundTrackDetector{
			DryOutboundTrackDetectorConfig{}
		};
		// Transport quality: a sustained high round trip.
		std::optional<TransportDelayDetectorConfig> transportDelayDetector{
			TransportDelayDetectorConfig{}
		};
		// Connectivity layer 5: ICE stays disconnected.
		std::optional<IceDisconnectedDetectorConfig> iceDisconnectedDetector{
			IceDisconnectedDetectorConfig{}
		};
	};
} // namespace observertc::clientmonitor

#endif
