#ifndef CM_CLIENT_MONITOR_HPP
#define CM_CLIENT_MONITOR_HPP

#include "clientmonitor/utils/IntervalTimer.hpp"
#include "clientmonitor/utils/IssueRegistry.hpp"
#include "clientmonitor/utils/SlicedWindow.hpp"
#include "clientmonitor/ClientMonitorConfig.hpp"
#include "clientmonitor/ClientMonitorIssues.hpp"
#include "clientmonitor/MediaTrack.hpp"
#include "clientmonitor/StatsCollector.hpp"
#include "clientmonitor/detectors/Detector.hpp"
#include "clientmonitor/monitors/PeerConnectionMonitor.hpp"
#include "clientmonitor/schema/SchemaTypes.hpp"
#include "clientmonitor/scores/ScoreCalculator.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace observertc::clientmonitor
{
	/**
	 * Monitors one WebRTC client: the peer connections it holds, the issues detected on them, and
	 * the events and samples it reports to an ObserveRTC observer. Port of the JS `ClientMonitor`.
	 *
	 * Threading: every public method is safe to call from any thread. Listener callbacks run on
	 * the thread that collects (the internal timer thread, or the caller of Collect()) while the
	 * monitor's lock is held, so a listener may call back into the monitor but must not block on
	 * a thread that is itself waiting for the monitor.
	 */
	class ClientMonitor : private IssueRegistrySink
	{
	public:
		/**
		 * Public Listener API, in the style of libmediasoupclient: override what you need, every
		 * method defaults to a no-op. Several listeners can be attached with AddListener().
		 */
		class Listener
		{
		public:
			struct StatsCollectedEvent
			{
				int64_t startedAt{ 0 };
				int64_t durationOfCollectingStatsInMs{ 0 };
				// The adapted report of every peer connection, keyed by peer connection id.
				std::vector<std::pair<std::string, nlohmann::json>> collectedStats;
			};

			struct StatsCollectionGapEvent
			{
				int64_t expectedPeriodInMs{ 0 };
				int64_t actualPeriodInMs{ 0 };
				int64_t gapInMs{ 0 };
			};

		public:
			virtual ~Listener() = default;

			/* Lifecycle and sampling. */
			virtual void OnSampleCreated(ClientMonitor* /*clientMonitor*/, const ClientSample& /*sample*/)
			{
			}
			virtual void OnStatsCollected(ClientMonitor* /*clientMonitor*/, const StatsCollectedEvent& /*event*/)
			{
			}
			virtual void OnClose(ClientMonitor* /*clientMonitor*/)
			{
			}
			virtual void OnScore(
			  ClientMonitor* /*clientMonitor*/,
			  double /*clientScore*/,
			  const std::map<std::string, double>& /*currentReasons*/)
			{
			}

			/* What the client reports. */
			virtual void OnIssue(ClientMonitor* /*clientMonitor*/, const ReportedClientIssue& /*issue*/)
			{
			}
			virtual void OnIssueUpdated(ClientMonitor* /*clientMonitor*/, const RaisedClientIssue& /*issue*/)
			{
			}
			virtual void OnIssueResolved(ClientMonitor* /*clientMonitor*/, const ResolvedClientIssue& /*issue*/)
			{
			}
			virtual void OnClientEvent(ClientMonitor* /*clientMonitor*/, const ClientEvent& /*event*/)
			{
			}
			virtual void OnMeta(ClientMonitor* /*clientMonitor*/, const ClientMetaData& /*metaData*/)
			{
			}
			virtual void OnExtensionStats(ClientMonitor* /*clientMonitor*/, const ExtensionStat& /*stats*/)
			{
			}

			/* Detector events. */
			virtual void OnStatsCollectionGap(
			  ClientMonitor* /*clientMonitor*/, const StatsCollectionGapEvent& /*event*/)
			{
			}
			virtual void OnDryInboundTrack(
			  ClientMonitor* /*clientMonitor*/, InboundTrackMonitor* /*trackMonitor*/)
			{
			}
			virtual void OnDryOutboundTrack(
			  ClientMonitor* /*clientMonitor*/, OutboundTrackMonitor* /*trackMonitor*/)
			{
			}
			virtual void OnTransportDelayDegraded(
			  ClientMonitor* /*clientMonitor*/,
			  PeerConnectionMonitor* /*peerConnectionMonitor*/,
			  double /*rttInMs*/)
			{
			}

			/* A monitor was created; the place to attach appData. */
			virtual void OnNewPeerConnectionMonitor(
			  ClientMonitor* /*clientMonitor*/, PeerConnectionMonitor* /*monitor*/)
			{
			}
			virtual void OnNewCodecMonitor(ClientMonitor* /*clientMonitor*/, CodecMonitor* /*monitor*/)
			{
			}
			virtual void OnNewInboundRtpMonitor(ClientMonitor* /*clientMonitor*/, InboundRtpMonitor* /*monitor*/)
			{
			}
			virtual void OnNewOutboundRtpMonitor(
			  ClientMonitor* /*clientMonitor*/, OutboundRtpMonitor* /*monitor*/)
			{
			}
			virtual void OnNewRemoteInboundRtpMonitor(
			  ClientMonitor* /*clientMonitor*/, RemoteInboundRtpMonitor* /*monitor*/)
			{
			}
			virtual void OnNewRemoteOutboundRtpMonitor(
			  ClientMonitor* /*clientMonitor*/, RemoteOutboundRtpMonitor* /*monitor*/)
			{
			}
			virtual void OnNewMediaSourceMonitor(
			  ClientMonitor* /*clientMonitor*/, MediaSourceMonitor* /*monitor*/)
			{
			}
			virtual void OnNewMediaPlayoutMonitor(
			  ClientMonitor* /*clientMonitor*/, MediaPlayoutMonitor* /*monitor*/)
			{
			}
			virtual void OnNewDataChannelMonitor(
			  ClientMonitor* /*clientMonitor*/, DataChannelMonitor* /*monitor*/)
			{
			}
			virtual void OnNewPeerConnectionTransportMonitor(
			  ClientMonitor* /*clientMonitor*/, PeerConnectionTransportMonitor* /*monitor*/)
			{
			}
			virtual void OnNewIceTransportMonitor(
			  ClientMonitor* /*clientMonitor*/, IceTransportMonitor* /*monitor*/)
			{
			}
			virtual void OnNewIceCandidateMonitor(
			  ClientMonitor* /*clientMonitor*/, IceCandidateMonitor* /*monitor*/)
			{
			}
			virtual void OnNewIceCandidatePairMonitor(
			  ClientMonitor* /*clientMonitor*/, IceCandidatePairMonitor* /*monitor*/)
			{
			}
			virtual void OnNewCertificateMonitor(
			  ClientMonitor* /*clientMonitor*/, CertificateMonitor* /*monitor*/)
			{
			}
			virtual void OnNewInboundTrackMonitor(
			  ClientMonitor* /*clientMonitor*/, InboundTrackMonitor* /*monitor*/)
			{
			}
			virtual void OnNewOutboundTrackMonitor(
			  ClientMonitor* /*clientMonitor*/, OutboundTrackMonitor* /*monitor*/)
			{
			}
		};

		/** Input for AddExtensionStats() and the extension stats providers. */
		struct ExtensionStatsInput
		{
			std::string type;
			nlohmann::json payload;
			// Giving an id is what makes the payload readable back with GetExtensionStatsPayload().
			std::optional<std::string> id;
		};

		using ExtensionStatsProvider = std::function<std::optional<ExtensionStatsInput>()>;

		/** The latest payload reported under one extension stats id. */
		struct ExtensionStatsEntry
		{
			std::string id;
			std::string type;
			nlohmann::json payload;
			// When the payload last arrived.
			int64_t timestamp{ 0 };
			bool visited{ true };
		};

		/** Client-wide aggregates over every peer connection, recomputed on every collection. */
		struct Metrics
		{
			double sendingAudioBitrate{ -1 };
			double sendingVideoBitrate{ -1 };
			double receivingAudioBitrate{ -1 };
			double receivingVideoBitrate{ -1 };
			double totalAvailableIncomingBitrate{ -1 };
			double totalAvailableOutgoingBitrate{ -1 };
			double avgRttInSec{ -1 };
			int64_t durationOfCollectingStatsInMs{ 0 };
			// CpuPerformanceDetector, once ported.
			bool cpuPerformanceAlertOn{ false };
			std::optional<double> cpuUtilization;
		};

		/** The running totals fed into the client's sliced window, by index. */
		enum WindowTotal : size_t
		{
			TOTAL_VIDEO_ENCODE_TIME_IN_MS = 0,
			TOTAL_VIDEO_DECODE_TIME_IN_MS,
			WINDOW_TOTALS_COUNT
		};

	public:
		explicit ClientMonitor(
		  const ClientMonitorConfig& config = ClientMonitorConfig(), Listener* listener = nullptr);
		~ClientMonitor() override;

		ClientMonitor(const ClientMonitor&)            = delete;
		ClientMonitor& operator=(const ClientMonitor&) = delete;

	public:
		/* Listeners. Not owned; must outlive the monitor or be removed first. */
		void AddListener(Listener* listener);
		void RemoveListener(Listener* listener);

		/* Identity. */
		std::optional<std::string> GetClientId() const;
		void SetClientId(const std::optional<std::string>& clientId);
		std::optional<std::string> GetCallId() const;
		void SetCallId(const std::optional<std::string>& callId);
		/** Additional data attached to every sample. */
		void SetAttachments(const nlohmann::json& attachments);
		nlohmann::json GetAttachments() const;
		const ClientMonitorConfig& GetConfig() const
		{
			return this->config;
		}
		nlohmann::json& GetAppData()
		{
			return this->config.appData;
		}
		int64_t GetCreatedAt() const
		{
			return this->createdAt;
		}
		int64_t GetUptimeInMs() const;
		bool IsClosed() const;

		/**
		 * Stops collecting, resolves every open issue, adds the CLIENT_LEFT event and a last sample
		 * if sampling, then reports OnClose(). Idempotent.
		 */
		void Close();

		/* Collection and sampling. */

		/**
		 * Collects stats from every peer connection and extension stats provider, updates every
		 * monitor and detector, and creates a sample when one is due. Called by the internal timer
		 * when collectingPeriodInMs > 0; call it yourself otherwise.
		 *
		 * StatsCollector::GetStats() is called without the monitor's lock held, so a collector that
		 * blocks on another thread (as libmediasoupclient's GetStats() does) cannot deadlock with
		 * that thread calling into the monitor.
		 */
		Listener::StatsCollectedEvent Collect();
		/** Builds a sample now, draining the buffered events, issues, meta and extension stats. */
		std::optional<ClientSample> CreateSample();
		void SetCollectingPeriod(int64_t collectingPeriodInMs);
		void SetSamplingPeriod(int64_t samplingPeriodInMs);
		int64_t GetLastSampledAt() const;
		int64_t GetLastCollectingStatsAt() const;
		const Metrics& GetMetrics() const
		{
			return this->metrics;
		}

		/* Sources. */

		/**
		 * Starts monitoring a peer connection whose stats come from `statsCollector`. An empty id
		 * gets a generated UUID. Adds a libwebrtc stats adapter by default, since native stats are
		 * Chromium's. Returns nullptr when closed or when the id is already monitored.
		 */
		PeerConnectionMonitor* AddPeerConnection(
		  std::shared_ptr<StatsCollector> statsCollector,
		  const std::string& peerConnectionId = "",
		  const nlohmann::json& attachments   = nlohmann::json());
		/** Closes and drops the peer connection monitor. */
		bool RemovePeerConnection(const std::string& peerConnectionId);

		/* Events, issues, meta. */
		void AddEvent(
		  const std::string& type,
		  const nlohmann::json& payload    = nlohmann::json(),
		  std::optional<int64_t> timestamp = std::nullopt);
		void AddClientJoinEvent(
		  const nlohmann::json& payload    = nlohmann::json(),
		  std::optional<int64_t> timestamp = std::nullopt);
		void AddClientLeftEvent(
		  const nlohmann::json& payload    = nlohmann::json(),
		  std::optional<int64_t> timestamp = std::nullopt);
		/** A moment rather than a condition: no key, nothing to resolve. */
		std::optional<AddedClientIssue> AddIssue(
		  const std::string& type,
		  const nlohmann::json& payload    = nlohmann::json(),
		  std::optional<int64_t> timestamp = std::nullopt,
		  bool includeInSample             = true);
		/** A condition with a lifetime. Re-raising an active key updates it in place. */
		const RaisedClientIssue* RaiseIssue(
		  const std::string& key,
		  const std::string& type,
		  const nlohmann::json& payload    = nlohmann::json(),
		  std::optional<int64_t> timestamp = std::nullopt,
		  bool includeInSample             = true);
		std::optional<ResolvedClientIssue> ResolveIssue(
		  const std::string& key,
		  const std::optional<std::string>& comment = std::nullopt,
		  const nlohmann::json& payload             = nlohmann::json(),
		  std::optional<int64_t> resolvedAt         = std::nullopt);
		bool IsIssueActive(const std::string& key) const;
		std::vector<RaisedClientIssue> GetActiveIssuesByType(const std::string& type) const;
		/** Every open issue, raised by the library or by the application. */
		IssueRegistry& GetActiveIssues()
		{
			return this->activeIssues;
		}
		void AddMetaData(
		  const std::string& type,
		  const nlohmann::json& payload    = nlohmann::json(),
		  std::optional<int64_t> timestamp = std::nullopt);

		/* Extension stats. */
		void AddExtensionStats(const ExtensionStatsInput& stats);
		/** Returns a handle for RemoveExtensionStatsProvider(). Providers run on every collection. */
		uint64_t AddExtensionStatsProvider(ExtensionStatsProvider provider);
		bool RemoveExtensionStatsProvider(uint64_t handle);
		std::optional<nlohmann::json> GetExtensionStatsPayload(const std::string& id) const;
		std::optional<ExtensionStatsEntry> GetExtensionStatsEntry(const std::string& id) const;

		/* Score. */
		void SetScore(
		  double score,
		  const std::map<std::string, double>& ownReasons        = {},
		  const std::map<std::string, double>& aggregatedReasons = {});
		double GetScore() const;
		std::map<std::string, double> GetScoreReasons() const;
		/** Installs the calculator run after every collection. nullptr removes it. */
		void SetScoreCalculator(std::unique_ptr<ScoreCalculator> scoreCalculator);

		/* The graph. Pointers stay valid until the next collection. */
		PeerConnectionMonitor* GetPeerConnectionMonitor(const std::string& peerConnectionId) const;
		std::vector<PeerConnectionMonitor*> GetPeerConnections() const;
		std::vector<InboundTrackMonitor*> GetInboundTracks() const;
		std::vector<OutboundTrackMonitor*> GetOutboundTracks() const;
		InboundTrackMonitor* GetInboundTrackMonitor(const std::string& trackId) const;
		OutboundTrackMonitor* GetOutboundTrackMonitor(const std::string& trackId) const;
		Detectors& GetDetectors()
		{
			return this->detectors;
		}
		const SlicedWindow& GetSlicedWindow() const
		{
			return *this->slicedWindow;
		}

		/* Context only the application knows; held pending until the track appears. */
		void SetInboundTrackContext(const std::string& trackId, const InboundTrackContext& context);
		void SetOutboundTrackContext(const std::string& trackId, const OutboundTrackContext& context);

		/**
		 * The window configuration for `numberOfTotals` totals: a "recovery" slice sits behind
		 * "detection", and "flowRecovery" behind "flowDetection", as in the JS library.
		 */
		SlicedWindow::Config CreateWindowConfig(const WindowConfig& windowConfig, size_t numberOfTotals) const;

		/**
		 * Calls `callback` with every listener. For detectors, including an application's own:
		 * the listeners are copied first, so a callback may add or remove listeners.
		 */
		template<typename F>
		void Emit(F&& callback)
		{
			const auto listeners = this->listeners;

			for (auto* listener : listeners)
			{
				callback(listener);
			}
		}

		/** The lock every public method takes. Recursive. */
		std::recursive_mutex& GetMutex() const
		{
			return this->mutex;
		}

	private:
		friend class PeerConnectionMonitor;

		void BufferIssueForSample(
		  const std::string& type,
		  const nlohmann::json& payload,
		  int64_t timestamp,
		  const std::optional<std::string>& key = std::nullopt);
		bool IsBuffering() const;
		void SetSamplingTick();
		void FlushBufferedClientSamples();
		std::optional<InboundTrackContext> TakePendingInboundTrackContext(const std::string& trackId);
		std::optional<OutboundTrackContext> TakePendingOutboundTrackContext(const std::string& trackId);
		void DropClosedPeerConnections();

		/* Pure virtual methods inherited from IssueRegistrySink: the end of the issue chain. */
	private:
		void OnIssueRaised(const RaisedClientIssue& issue) override;
		void OnIssueUpdated(const RaisedClientIssue& issue) override;
		void OnIssueResolved(const ResolvedClientIssue& issue) override;
		void OnIssueNotified(const AddedClientIssue& issue) override;

	private:
		mutable std::recursive_mutex mutex;
		ClientMonitorConfig config;
		int64_t createdAt{ 0 };
		bool closed{ false };
		std::vector<Listener*> listeners;
		IntervalTimer timer;

		nlohmann::json attachments;
		Metrics metrics;
		double score{ 5.0 };
		std::map<std::string, double> scoreReasons;
		int64_t lastSampledAt{ 0 };
		int64_t lastCollectingStatsAt{ 0 };
		int64_t samplingTick{ 0 };
		int64_t collectingCounter{ 0 };

		std::vector<ClientEvent> clientEvents;
		std::vector<ClientMetaData> clientMetaItems;
		std::vector<ClientIssue> clientIssues;
		std::vector<ExtensionStat> extensionStats;
		std::optional<std::vector<ClientSample>> bufferedClientSamples;

		std::map<std::string, ExtensionStatsEntry> extensionStatsEntries;
		uint64_t nextProviderHandle{ 1 };
		std::map<uint64_t, ExtensionStatsProvider> extensionStatsProviders;
		std::map<std::string, InboundTrackContext> pendingInboundTrackContexts;
		std::map<std::string, OutboundTrackContext> pendingOutboundTrackContexts;

		IssueRegistry activeIssues;
		std::unique_ptr<SlicedWindow> slicedWindow;
		// Shared so a collection in flight keeps a monitor alive while the lock is released.
		std::map<std::string, std::shared_ptr<PeerConnectionMonitor>> peerConnections;
		std::unique_ptr<ScoreCalculator> scoreCalculator;
		Detectors detectors;
	};
} // namespace observertc::clientmonitor

#endif
