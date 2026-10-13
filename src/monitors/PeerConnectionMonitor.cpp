#define CM_CLASS "PeerConnectionMonitor"

#include "clientmonitor/monitors/PeerConnectionMonitor.hpp"
#include "clientmonitor/utils/JsonReader.hpp"
#include "clientmonitor/utils/MediaMath.hpp"
#include "clientmonitor/utils/Utils.hpp"
#include "clientmonitor/ClientMonitor.hpp"
#include "clientmonitor/Logger.hpp"
#include "clientmonitor/detectors/IceDisconnectedDetector.hpp"
#include "clientmonitor/detectors/TransportDelayDetector.hpp"
#include "clientmonitor/schema/ClientEventTypes.hpp"

#include <algorithm>
#include <initializer_list>

using json = nlohmann::json;
using clientmonitor::utils::AccumulatedValue;

namespace clientmonitor
{
	namespace
	{
		bool HasFields(const json& raw, std::initializer_list<const char*> fields)
		{
			for (const auto* field : fields)
			{
				auto it = raw.find(field);

				if (it == raw.end() || it->is_null())
				{
					return false;
				}
			}

			return true;
		}

		template<typename Map>
		auto CollectValues(const Map& map)
		{
			std::vector<typename Map::mapped_type::pointer> result;

			result.reserve(map.size());

			for (const auto& [key, monitor] : map)
			{
				result.push_back(monitor.get());
			}

			return result;
		}

		template<typename Map, typename Key>
		auto FindIn(const Map& map, const Key& key) -> typename Map::mapped_type::pointer
		{
			auto it = map.find(key);

			return it == map.end() ? nullptr : it->second.get();
		}

		const std::map<std::string, int>& LimitationPriority()
		{
			static const std::map<std::string, int> Priority = {
				{ "none",      1 },
				{ "other",     2 },
				{ "cpu",       3 },
				{ "bandwidth", 4 },
			};

			return Priority;
		}

		const std::map<std::string, int>& IceStateSeverity()
		{
			static const std::map<std::string, int> Severity = {
				{ "failed",       6 },
        { "disconnected", 5 },
        { "checking",     4 },
        { "new",          3 },
				{ "connected",    2 },
        { "completed",    1 },
        { "closed",       0 },
			};

			return Severity;
		}
	} // namespace

	PeerConnectionMonitor::PeerConnectionMonitor(
	  ClientMonitor* parent,
	  std::string peerConnectionId,
	  std::shared_ptr<StatsCollector> statsCollector,
	  const json& attachments)
	  : parent(parent),
	    peerConnectionId(std::move(peerConnectionId)),
	    statsCollector(std::move(statsCollector)),
	    attachments(attachments),
	    issues(parent->GetActiveIssues().AsSink())
	{
		CM_TRACE();

		const auto& config = parent->GetConfig();

		this->slicedWindow = std::make_unique<SlicedWindow>(
		  parent->CreateWindowConfig(config.peerConnectionWindow, WINDOW_TOTALS_COUNT));

		// Registered in connectivity-layer order for readability only: every detector reaches its
		// verdict from raw stats, so the run order carries no meaning.
		if (config.iceDisconnectedDetector.has_value())
		{
			this->detectors.Add(std::make_unique<IceDisconnectedDetector>(this));
		}
		if (config.transportDelayDetector.has_value())
		{
			this->detectors.Add(std::make_unique<TransportDelayDetector>(this));
		}

		// The remaining connection-level detectors of the JS library (ICE reachability, DTLS,
		// congestion, transport loss, ...) register here as they are ported.
	}

	PeerConnectionMonitor::~PeerConnectionMonitor()
	{
		CM_TRACE();

		// Tracks reference the rtp and media source monitors; drop them first.
		this->inboundTracks.clear();
		this->outboundTracks.clear();
	}

	json PeerConnectionMonitor::Collect()
	{
		CM_TRACE();

		json stats = this->statsCollector ? this->statsCollector->GetStats() : json::array();

		std::lock_guard<std::recursive_mutex> lock(this->parent->GetMutex());

		if (this->closed)
		{
			return json::array();
		}

		for (auto& adapter : this->statsAdapters)
		{
			try
			{
				adapter->Adapt(stats);
			}
			catch (const std::exception& error)
			{
				CM_WARN("error adapting stats with %s: %s", adapter->GetName().c_str(), error.what());
			}
		}

		this->AcceptAdaptedStats(stats);

		return stats;
	}

	void PeerConnectionMonitor::Accept(json stats)
	{
		std::lock_guard<std::recursive_mutex> lock(this->parent->GetMutex());

		if (this->closed)
		{
			return;
		}

		for (auto& adapter : this->statsAdapters)
		{
			try
			{
				adapter->Adapt(stats);
			}
			catch (const std::exception& error)
			{
				CM_WARN("error adapting stats with %s: %s", adapter->GetName().c_str(), error.what());
			}
		}

		this->AcceptAdaptedStats(stats);
	}

	void PeerConnectionMonitor::AddStatsAdapter(std::unique_ptr<StatsAdapter> adapter)
	{
		std::lock_guard<std::recursive_mutex> lock(this->parent->GetMutex());

		if (!adapter)
		{
			return;
		}

		for (const auto& existing : this->statsAdapters)
		{
			if (existing->GetName() == adapter->GetName())
			{
				CM_WARN("adapter with name %s already exists", adapter->GetName().c_str());

				return;
			}
		}

		this->statsAdapters.push_back(std::move(adapter));
	}

	bool PeerConnectionMonitor::RemoveStatsAdapter(const std::string& name)
	{
		std::lock_guard<std::recursive_mutex> lock(this->parent->GetMutex());

		const auto size = this->statsAdapters.size();

		this->statsAdapters.erase(
		  std::remove_if(
		    this->statsAdapters.begin(),
		    this->statsAdapters.end(),
		    [&name](const std::unique_ptr<StatsAdapter>& adapter)
		    {
			    return adapter->GetName() == name;
		    }),
		  this->statsAdapters.end());

		return size != this->statsAdapters.size();
	}

	void PeerConnectionMonitor::AcceptAdaptedStats(const json& stats)
	{
		if (!stats.is_array())
		{
			CM_WARN("stats report is not an array, ignoring it");

			return;
		}

		auto& m = this->metrics;

		// Computed first, so deltaTime is available to everything downstream this tick.
		std::optional<double> newestTimestamp;

		for (const auto& item : stats)
		{
			const auto timestamp = detail::ReadDouble(item, "timestamp");

			if (timestamp.has_value() && (!newestTimestamp.has_value() || *newestTimestamp < *timestamp))
			{
				newestTimestamp = timestamp;
			}
		}

		const auto timeDelta =
		  newestTimestamp.has_value() && this->previousNewestTimestamp.has_value()
		    ? std::optional<double>(*newestTimestamp - *this->previousNewestTimestamp)
		    : std::nullopt;

		m.deltaTime = timeDelta.has_value() && 0 < *timeDelta ? timeDelta : std::nullopt;
		m.statsClockTime += m.deltaTime.value_or(0);

		if (newestTimestamp.has_value())
		{
			this->previousNewestTimestamp = newestTimestamp;
		}

		// Kept apart: RTCP and ICE round trips measure different paths.
		std::vector<double> rtcpRttMeasurementsInS;
		std::vector<double> iceRttMeasurementsInS;

		m.deltaVideoBytesSent.reset();
		m.deltaAudioBytesSent.reset();
		m.deltaVideoBytesReceived.reset();
		m.deltaAudioBytesReceived.reset();
		m.deltaDataChannelBytesReceived.reset();
		m.deltaDataChannelBytesSent.reset();
		m.deltaOutboundPacketsLost.reset();
		m.deltaOutboundPacketsReceived.reset();
		m.deltaOutboundPacketsSent.reset();
		m.deltaInboundPacketsLost.reset();
		m.deltaInboundPacketsReceived.reset();
		m.deltaPacketSendDelayInSec.reset();
		m.deltaInboundVideoJitterBufferDelayInSec.reset();
		m.deltaInboundVideoJitterBufferEmittedCount.reset();
		m.avgInboundVideoJitterBufferDelayInMs.reset();
		m.deltaVideoPacketsSent.reset();
		m.deltaVideoEncodeTimeInMs.reset();
		m.deltaVideoDecodeTimeInMs.reset();
		m.avgPacketSendDelayInMs.reset();

		m.sendingAudioBitrate         = 0;
		m.sendingVideoBitrate         = 0;
		m.receivingAudioBitrate       = 0;
		m.receivingVideoBitrate       = 0;
		m.dataChannelSendingBitrate   = 0;
		m.dataChannelReceivingBitrate = 0;
		m.outboundFractionLost        = 0;
		m.inboundFractionalLost       = 0;
		m.totalAvailableIncomingBitrate.reset();
		m.totalAvailableOutgoingBitrate.reset();
		m.qualityLimitationReason.reset();
		m.hasInboundMedia  = false;
		m.hasOutboundMedia = false;
		m.hasInboundVideo  = false;

		for (const auto& item : stats)
		{
			const auto type = detail::ReadString(item, "type");

			if (!type.has_value())
			{
				continue;
			}

			if (*type == "codec")
			{
				this->UpdateCodec(item);
			}
			else if (*type == "inbound-rtp")
			{
				auto* monitor = this->UpdateInboundRtp(item);

				if (!monitor)
				{
					continue;
				}

				const auto& rm = monitor->GetMetrics();
				const auto& rs = monitor->GetStats();

				if (rs.kind == "audio")
				{
					m.receivingAudioBitrate += rm.bitrate.value_or(0);
					m.deltaAudioBytesReceived =
					  AccumulatedValue(m.deltaAudioBytesReceived, rm.deltaBytesReceived);
					m.hasInboundMedia = true;
				}
				else if (rs.kind == "video")
				{
					m.receivingVideoBitrate += rm.bitrate.value_or(0);
					m.deltaVideoBytesReceived =
					  AccumulatedValue(m.deltaVideoBytesReceived, rm.deltaBytesReceived);
					m.hasInboundMedia = true;
					m.hasInboundVideo = true;

					if (!utils::RunsOffCpu(rs.decoderImplementation, rs.powerEfficientDecoder))
					{
						m.deltaVideoDecodeTimeInMs = AccumulatedValue(
						  m.deltaVideoDecodeTimeInMs,
						  rm.deltaTotalDecodeTime.has_value()
						    ? std::optional<double>(*rm.deltaTotalDecodeTime * 1000)
						    : std::nullopt);
					}
					// Summed rather than averaged over streams: the quotient of the two sums is
					// the mean over frames.
					if (rm.deltaJitterBufferEmittedCount.value_or(0) != 0)
					{
						m.deltaInboundVideoJitterBufferDelayInSec =
						  AccumulatedValue(m.deltaInboundVideoJitterBufferDelayInSec, rm.deltaJitterBufferDelay);
						m.deltaInboundVideoJitterBufferEmittedCount = AccumulatedValue(
						  m.deltaInboundVideoJitterBufferEmittedCount, rm.deltaJitterBufferEmittedCount);
					}
				}

				m.inboundFractionalLost += rm.deltaFractionLost.value_or(0);
				m.deltaInboundPacketsLost = AccumulatedValue(m.deltaInboundPacketsLost, rm.deltaPacketsLost);
				m.deltaInboundPacketsReceived =
				  AccumulatedValue(m.deltaInboundPacketsReceived, rm.deltaPacketsReceived);
			}
			else if (*type == "remote-outbound-rtp")
			{
				auto* monitor = this->UpdateRemoteOutboundRtp(item);

				// Only when this report is new: getStats() keeps serving the last sender report,
				// and re-averaging it would make stopped RTCP read as healthy.
				if (monitor && monitor->GetStats().roundTripTime.has_value() && 0 < monitor->GetDeltaTime().value_or(0))
				{
					rtcpRttMeasurementsInS.push_back(*monitor->GetStats().roundTripTime);
				}
			}
			else if (*type == "outbound-rtp")
			{
				auto* monitor = this->UpdateOutboundRtp(item);

				if (!monitor)
				{
					continue;
				}

				const auto& rm = monitor->GetMetrics();
				const auto& rs = monitor->GetStats();

				if (rs.kind == "audio")
				{
					m.sendingAudioBitrate += rm.bitrate.value_or(0);
					m.deltaAudioBytesSent = AccumulatedValue(m.deltaAudioBytesSent, rm.deltaBytesSent);
					m.hasOutboundMedia    = true;
				}
				else if (rs.kind == "video")
				{
					m.sendingVideoBitrate += rm.bitrate.value_or(0);
					m.deltaVideoBytesSent = AccumulatedValue(m.deltaVideoBytesSent, rm.deltaBytesSent);

					if (!utils::RunsOffCpu(rs.encoderImplementation, rs.powerEfficientEncoder))
					{
						m.deltaVideoEncodeTimeInMs = AccumulatedValue(
						  m.deltaVideoEncodeTimeInMs,
						  rm.deltaEncodeTime.has_value() ? std::optional<double>(*rm.deltaEncodeTime * 1000)
						                                 : std::nullopt);
					}

					m.totalPacketSendDelayInSec =
					  AccumulatedValue(m.totalPacketSendDelayInSec, rm.deltaPacketSendDelay);
					m.deltaPacketSendDelayInSec =
					  AccumulatedValue(m.deltaPacketSendDelayInSec, rm.deltaPacketSendDelay);
					m.deltaVideoPacketsSent = AccumulatedValue(m.deltaVideoPacketsSent, rm.deltaPacketsSent);
					m.hasOutboundMedia      = true;
				}

				m.deltaOutboundPacketsSent =
				  AccumulatedValue(m.deltaOutboundPacketsSent, rm.deltaPacketsSent);

				// Only a stream that sent this collection gets a vote: a paused sender keeps
				// reporting whatever limited it when it stopped.
				if (0 < rm.deltaPacketsSent.value_or(0))
				{
					this->UpdatePeerConnectionLimitationReason(rs.qualityLimitationReason);
				}
			}
			else if (*type == "remote-inbound-rtp")
			{
				auto* monitor = this->UpdateRemoteInboundRtp(item);

				if (!monitor)
				{
					continue;
				}

				// The canonical RTCP round trip, counted only when the report is new.
				if (monitor->GetStats().roundTripTime.has_value() && 0 < monitor->GetDeltaTime().value_or(0))
				{
					rtcpRttMeasurementsInS.push_back(*monitor->GetStats().roundTripTime);
				}

				const auto& rm = monitor->GetMetrics();

				m.outboundFractionLost += rm.deltaFractionLost.value_or(0);
				m.deltaOutboundPacketsLost =
				  AccumulatedValue(m.deltaOutboundPacketsLost, rm.deltaPacketsLost);
				m.deltaOutboundPacketsReceived =
				  AccumulatedValue(m.deltaOutboundPacketsReceived, rm.deltaPacketsReceived);
			}
			else if (*type == "data-channel")
			{
				auto* monitor = this->UpdateDataChannel(item);

				if (!monitor)
				{
					continue;
				}

				const auto& dm = monitor->GetMetrics();

				m.deltaDataChannelBytesSent =
				  AccumulatedValue(m.deltaDataChannelBytesSent, dm.deltaBytesSent);
				m.deltaDataChannelBytesReceived =
				  AccumulatedValue(m.deltaDataChannelBytesReceived, dm.deltaBytesReceived);
				m.dataChannelSendingBitrate += dm.sendingBitrate.value_or(0);
				m.dataChannelReceivingBitrate += dm.receivingBitrate.value_or(0);
			}
			else if (*type == "media-source")
			{
				this->UpdateMediaSource(item);
			}
			else if (*type == "media-playout")
			{
				this->UpdateMediaPlayout(item);
			}
			else if (*type == "transport")
			{
				// What the selected pair carries is read after the loop: `transport` only names
				// the pair by id, and that pair may not have been accepted yet.
				this->UpdateIceTransport(item);
			}
			else if (*type == "peer-connection")
			{
				this->UpdatePeerConnectionTransport(item);
			}
			else if (*type == "local-candidate")
			{
				this->UpdateIceCandidate(item, IceCandidateMonitor::Direction::LOCAL);
			}
			else if (*type == "remote-candidate")
			{
				this->UpdateIceCandidate(item, IceCandidateMonitor::Direction::REMOTE);
			}
			else if (*type == "candidate-pair")
			{
				this->UpdateIceCandidatePair(item);
			}
			else if (*type == "certificate")
			{
				this->UpdateCertificate(item);
			}
			else
			{
				CM_DEBUG("unknown stats type %s", type->c_str());
			}
		}

		this->CheckVisited();

		if (!rtcpRttMeasurementsInS.empty())
		{
			double sum{ 0 };

			for (auto rtt : rtcpRttMeasurementsInS)
			{
				sum += rtt;
			}

			m.rtcpRttInSec     = sum / static_cast<double>(rtcpRttMeasurementsInS.size());
			m.ewmaRtcpRttInSec = m.ewmaRtcpRttInSec.has_value()
			                       ? (*m.rtcpRttInSec * 0.1) + (*m.ewmaRtcpRttInSec * 0.9)
			                       : *m.rtcpRttInSec;
		}

		this->UpdateTransportQualityAverages();

		const auto selectedPairs = this->GetSelectedIceCandidatePairs();

		for (auto* pair : selectedPairs)
		{
			const auto& ps = pair->GetStats();

			m.totalAvailableIncomingBitrate =
			  AccumulatedValue(m.totalAvailableIncomingBitrate, ps.availableIncomingBitrate);
			m.totalAvailableOutgoingBitrate =
			  AccumulatedValue(m.totalAvailableOutgoingBitrate, ps.availableOutgoingBitrate);

			// Interval average when a check completed this tick; the latest one otherwise.
			const auto iceRtt = pair->GetMetrics().avgRoundTripTimeInSec.has_value()
			                      ? pair->GetMetrics().avgRoundTripTimeInSec
			                      : ps.currentRoundTripTime;

			if (iceRtt.has_value())
			{
				iceRttMeasurementsInS.push_back(*iceRtt);
			}
		}

		if (!iceRttMeasurementsInS.empty())
		{
			double sum{ 0 };

			for (auto rtt : iceRttMeasurementsInS)
			{
				sum += rtt;
			}

			m.iceRttInSec     = sum / static_cast<double>(iceRttMeasurementsInS.size());
			m.ewmaIceRttInSec = m.ewmaIceRttInSec.has_value()
			                      ? (*m.iceRttInSec * 0.1) + (*m.ewmaIceRttInSec * 0.9)
			                      : *m.iceRttInSec;
		}

		// SelectedIcePath (ice-path-changed events) is not ported yet.

		m.usingTCP = std::any_of(
		  selectedPairs.begin(),
		  selectedPairs.end(),
		  [](IceCandidatePairMonitor* pair)
		  {
			  return pair->IsUsingTcp();
		  });
		m.usingTURN = std::any_of(
		  selectedPairs.begin(),
		  selectedPairs.end(),
		  [](IceCandidatePairMonitor* pair)
		  {
			  return pair->IsUsingTurn();
		  });
		// Most severe across the transports, so a failed one is not masked by a healthy sibling.
		m.iceState = this->MostSevereIceState();

		// Undefined unless packets actually carried the delay.
		m.avgPacketSendDelayInMs =
		  m.deltaPacketSendDelayInSec.has_value() && m.deltaVideoPacketsSent.value_or(0) > 0
		    ? std::optional<double>((*m.deltaPacketSendDelayInSec * 1000) / *m.deltaVideoPacketsSent)
		    : std::nullopt;
		m.avgInboundVideoJitterBufferDelayInMs =
		  m.deltaInboundVideoJitterBufferDelayInSec.has_value() &&
		      m.deltaInboundVideoJitterBufferEmittedCount.value_or(0) > 0
		    ? std::optional<double>(
		        (*m.deltaInboundVideoJitterBufferDelayInSec * 1000) /
		        *m.deltaInboundVideoJitterBufferEmittedCount)
		    : std::nullopt;

		m.totalDataChannelBytesReceived =
		  AccumulatedValue(m.totalDataChannelBytesReceived, m.deltaDataChannelBytesReceived);
		m.totalDataChannelBytesSent =
		  AccumulatedValue(m.totalDataChannelBytesSent, m.deltaDataChannelBytesSent);
		m.totalSentAudioBytes = AccumulatedValue(m.totalSentAudioBytes, m.deltaAudioBytesSent);
		m.totalSentVideoBytes = AccumulatedValue(m.totalSentVideoBytes, m.deltaVideoBytesSent);
		m.totalReceivedAudioBytes =
		  AccumulatedValue(m.totalReceivedAudioBytes, m.deltaAudioBytesReceived);
		m.totalReceivedVideoBytes =
		  AccumulatedValue(m.totalReceivedVideoBytes, m.deltaVideoBytesReceived);
		m.totalVideoEncodeTimeInMs =
		  AccumulatedValue(m.totalVideoEncodeTimeInMs, m.deltaVideoEncodeTimeInMs);
		m.totalVideoDecodeTimeInMs =
		  AccumulatedValue(m.totalVideoDecodeTimeInMs, m.deltaVideoDecodeTimeInMs);
		m.totalOutboundPacketsSent =
		  AccumulatedValue(m.totalOutboundPacketsSent, m.deltaOutboundPacketsSent);
		m.totalOutboundPacketsReceived =
		  AccumulatedValue(m.totalOutboundPacketsReceived, m.deltaOutboundPacketsReceived);
		m.totalOutboundPacketsLost =
		  AccumulatedValue(m.totalOutboundPacketsLost, m.deltaOutboundPacketsLost);
		m.totalInboundPacketsLost =
		  AccumulatedValue(m.totalInboundPacketsLost, m.deltaInboundPacketsLost);
		m.totalInboundPacketsReceived =
		  AccumulatedValue(m.totalInboundPacketsReceived, m.deltaInboundPacketsReceived);

		this->RefreshTransportStability();
		this->FeedSlicedWindow();

		this->detectors.Update();

		// Transport-bound detectors run on the same tick and from the same stats.
		for (auto* iceTransport : this->GetIceTransports())
		{
			iceTransport->GetDetectors().Update();
		}
	}

	void PeerConnectionMonitor::AddMediaTrack(std::shared_ptr<MediaTrack> track, const json& attachments)
	{
		std::lock_guard<std::recursive_mutex> lock(this->parent->GetMutex());

		if (this->closed || !track || track->GetReadyState() == MediaTrack::ReadyState::ENDED)
		{
			return;
		}

		const auto trackId = track->GetId();

		for (auto* mediaSource : this->GetMediaSources())
		{
			if (mediaSource->GetStats().trackIdentifier == trackId)
			{
				return this->CreateOutboundTrackMonitor(track, mediaSource, attachments);
			}
		}
		for (auto* inboundRtp : this->GetInboundRtps())
		{
			if (inboundRtp->GetStats().trackIdentifier == trackId)
			{
				return this->CreateInboundTrackMonitor(track, inboundRtp, attachments);
			}
		}

		this->pendingTracks[trackId] = PendingTrack{ std::move(track), attachments };
	}

	void PeerConnectionMonitor::OnMediaTrackEnded(const std::string& trackId)
	{
		std::lock_guard<std::recursive_mutex> lock(this->parent->GetMutex());

		this->pendingTracks.erase(trackId);

		auto inboundIt = this->inboundTracks.find(trackId);

		if (inboundIt != this->inboundTracks.end())
		{
			inboundIt->second->GetIssues().ResolveAll("the track ended");
			this->inboundTracks.erase(inboundIt);
		}

		// An outbound track is flagged rather than dropped: the detector whose subject a lost
		// capture source is still needs a last look. CheckVisited() drops it afterwards.
		auto outboundIt = this->outboundTracks.find(trackId);

		if (outboundIt != this->outboundTracks.end())
		{
			outboundIt->second->SetSourceEnded(true);
		}
	}

	void PeerConnectionMonitor::SetConnectionState(const std::string& state)
	{
		std::lock_guard<std::recursive_mutex> lock(this->parent->GetMutex());

		if (this->connectionState == state)
		{
			return;
		}

		this->connectionState = state;

		if (state == "connecting")
		{
			this->connectingStartedAt = utils::NowMs();
		}
		else if (state == "connected")
		{
			this->connectedAt = utils::NowMs();
		}
		else
		{
			this->connectingStartedAt.reset();
			this->connectedAt.reset();
		}
	}

	PeerConnectionSample PeerConnectionMonitor::CreateSample()
	{
		PeerConnectionSample sample;

		sample.peerConnectionId = this->peerConnectionId;
		sample.attachments      = this->attachments;
		sample.score            = this->calculatedStabilityScore.value;
		sample.scoreReasons     = utils::SampledScoreReasons(
      this->calculatedStabilityScore.reasons, this->parent->GetConfig().sendScoreReasonsToServer);

		// Every list is present, if empty, as in the JS samples.
		sample.codecs.emplace();
		sample.inboundRtps.emplace();
		sample.remoteOutboundRtps.emplace();
		sample.outboundRtps.emplace();
		sample.remoteInboundRtps.emplace();
		sample.mediaSources.emplace();
		sample.mediaPlayouts.emplace();
		sample.peerConnectionTransports.emplace();
		sample.dataChannels.emplace();
		sample.iceTransports.emplace();
		sample.iceCandidates.emplace();
		sample.iceCandidatePairs.emplace();
		sample.certificates.emplace();
		sample.inboundTracks.emplace();
		sample.outboundTracks.emplace();

		for (auto& [id, monitor] : this->codecs)
		{
			sample.codecs->push_back(monitor->CreateSample());
		}
		for (auto& [ssrc, monitor] : this->inboundRtps)
		{
			sample.inboundRtps->push_back(monitor->CreateSample());
		}
		for (auto& [ssrc, monitor] : this->remoteOutboundRtps)
		{
			sample.remoteOutboundRtps->push_back(monitor->CreateSample());
		}
		for (auto& [ssrc, monitor] : this->outboundRtps)
		{
			sample.outboundRtps->push_back(monitor->CreateSample());
		}
		for (auto& [ssrc, monitor] : this->remoteInboundRtps)
		{
			sample.remoteInboundRtps->push_back(monitor->CreateSample());
		}
		for (auto& [id, monitor] : this->mediaSources)
		{
			sample.mediaSources->push_back(monitor->CreateSample());
		}
		for (auto& [id, monitor] : this->mediaPlayouts)
		{
			sample.mediaPlayouts->push_back(monitor->CreateSample());
		}
		for (auto& [id, monitor] : this->peerConnectionTransports)
		{
			sample.peerConnectionTransports->push_back(monitor->CreateSample());
		}
		for (auto& [id, monitor] : this->dataChannels)
		{
			sample.dataChannels->push_back(monitor->CreateSample());
		}
		for (auto& [id, monitor] : this->iceTransports)
		{
			sample.iceTransports->push_back(monitor->CreateSample());
		}
		for (auto& [id, monitor] : this->iceCandidates)
		{
			sample.iceCandidates->push_back(monitor->CreateSample());
		}
		for (auto& [id, monitor] : this->iceCandidatePairs)
		{
			sample.iceCandidatePairs->push_back(monitor->CreateSample());
		}
		for (auto& [id, monitor] : this->certificates)
		{
			sample.certificates->push_back(monitor->CreateSample());
		}
		for (auto& [id, monitor] : this->inboundTracks)
		{
			sample.inboundTracks->push_back(monitor->CreateSample());
		}
		for (auto& [id, monitor] : this->outboundTracks)
		{
			sample.outboundTracks->push_back(monitor->CreateSample());
		}

		return sample;
	}

	void PeerConnectionMonitor::Close()
	{
		std::lock_guard<std::recursive_mutex> lock(this->parent->GetMutex());

		if (this->closed)
		{
			return;
		}

		this->closed = true;

		// Twice: on the second pass every monitor is unvisited, so all of them are dropped.
		this->CheckVisited();
		this->CheckVisited();

		this->issues.ResolveAll("peer connection closed");
	}

	std::optional<double> PeerConnectionMonitor::GetAvgRttInSec() const
	{
		return this->metrics.rtcpRttInSec.has_value() ? this->metrics.rtcpRttInSec
		                                              : this->metrics.iceRttInSec;
	}

	std::optional<double> PeerConnectionMonitor::GetEwmaRttInSec() const
	{
		return this->metrics.rtcpRttInSec.has_value() ? this->metrics.ewmaRtcpRttInSec
		                                              : this->metrics.ewmaIceRttInSec;
	}

	double PeerConnectionMonitor::GetSendingBitrate() const
	{
		return this->metrics.sendingAudioBitrate + this->metrics.sendingVideoBitrate +
		       this->metrics.dataChannelSendingBitrate;
	}

	double PeerConnectionMonitor::GetReceivingBitrate() const
	{
		return this->metrics.receivingAudioBitrate + this->metrics.receivingVideoBitrate +
		       this->metrics.dataChannelReceivingBitrate;
	}

	/* The graph. */

	std::vector<CodecMonitor*> PeerConnectionMonitor::GetCodecs() const
	{
		return CollectValues(this->codecs);
	}

	std::vector<InboundRtpMonitor*> PeerConnectionMonitor::GetInboundRtps() const
	{
		return CollectValues(this->inboundRtps);
	}

	std::vector<OutboundRtpMonitor*> PeerConnectionMonitor::GetOutboundRtps() const
	{
		return CollectValues(this->outboundRtps);
	}

	std::vector<RemoteInboundRtpMonitor*> PeerConnectionMonitor::GetRemoteInboundRtps() const
	{
		return CollectValues(this->remoteInboundRtps);
	}

	std::vector<RemoteOutboundRtpMonitor*> PeerConnectionMonitor::GetRemoteOutboundRtps() const
	{
		return CollectValues(this->remoteOutboundRtps);
	}

	std::vector<MediaSourceMonitor*> PeerConnectionMonitor::GetMediaSources() const
	{
		return CollectValues(this->mediaSources);
	}

	std::vector<MediaPlayoutMonitor*> PeerConnectionMonitor::GetMediaPlayouts() const
	{
		return CollectValues(this->mediaPlayouts);
	}

	std::vector<DataChannelMonitor*> PeerConnectionMonitor::GetDataChannels() const
	{
		return CollectValues(this->dataChannels);
	}

	std::vector<PeerConnectionTransportMonitor*> PeerConnectionMonitor::GetPeerConnectionTransports() const
	{
		return CollectValues(this->peerConnectionTransports);
	}

	std::vector<IceTransportMonitor*> PeerConnectionMonitor::GetIceTransports() const
	{
		return CollectValues(this->iceTransports);
	}

	std::vector<IceCandidateMonitor*> PeerConnectionMonitor::GetIceCandidates() const
	{
		return CollectValues(this->iceCandidates);
	}

	std::vector<IceCandidateMonitor*> PeerConnectionMonitor::GetLocalIceCandidates() const
	{
		std::vector<IceCandidateMonitor*> result;

		for (const auto& [id, candidate] : this->iceCandidates)
		{
			if (candidate->GetDirection() == IceCandidateMonitor::Direction::LOCAL)
			{
				result.push_back(candidate.get());
			}
		}

		return result;
	}

	std::vector<IceCandidatePairMonitor*> PeerConnectionMonitor::GetIceCandidatePairs() const
	{
		return CollectValues(this->iceCandidatePairs);
	}

	std::vector<IceCandidatePairMonitor*> PeerConnectionMonitor::GetSelectedIceCandidatePairs() const
	{
		std::vector<IceCandidatePairMonitor*> result;

		for (const auto& [id, transport] : this->iceTransports)
		{
			if (auto* pair = transport->GetSelectedCandidatePair())
			{
				result.push_back(pair);
			}
		}

		return result;
	}

	std::vector<CertificateMonitor*> PeerConnectionMonitor::GetCertificates() const
	{
		return CollectValues(this->certificates);
	}

	std::vector<InboundTrackMonitor*> PeerConnectionMonitor::GetInboundTracks() const
	{
		return CollectValues(this->inboundTracks);
	}

	std::vector<OutboundTrackMonitor*> PeerConnectionMonitor::GetOutboundTracks() const
	{
		return CollectValues(this->outboundTracks);
	}

	CodecMonitor* PeerConnectionMonitor::GetCodecMonitor(const std::string& id) const
	{
		return FindIn(this->codecs, id);
	}

	InboundRtpMonitor* PeerConnectionMonitor::GetInboundRtpMonitor(int64_t ssrc) const
	{
		return FindIn(this->inboundRtps, ssrc);
	}

	OutboundRtpMonitor* PeerConnectionMonitor::GetOutboundRtpMonitor(int64_t ssrc) const
	{
		return FindIn(this->outboundRtps, ssrc);
	}

	RemoteInboundRtpMonitor* PeerConnectionMonitor::GetRemoteInboundRtpMonitor(int64_t ssrc) const
	{
		return FindIn(this->remoteInboundRtps, ssrc);
	}

	RemoteOutboundRtpMonitor* PeerConnectionMonitor::GetRemoteOutboundRtpMonitor(int64_t ssrc) const
	{
		return FindIn(this->remoteOutboundRtps, ssrc);
	}

	MediaSourceMonitor* PeerConnectionMonitor::GetMediaSourceMonitor(const std::string& id) const
	{
		return FindIn(this->mediaSources, id);
	}

	MediaPlayoutMonitor* PeerConnectionMonitor::GetMediaPlayoutMonitor(const std::string& id) const
	{
		return FindIn(this->mediaPlayouts, id);
	}

	DataChannelMonitor* PeerConnectionMonitor::GetDataChannelMonitor(const std::string& id) const
	{
		return FindIn(this->dataChannels, id);
	}

	IceTransportMonitor* PeerConnectionMonitor::GetIceTransportMonitor(const std::string& id) const
	{
		return FindIn(this->iceTransports, id);
	}

	IceCandidateMonitor* PeerConnectionMonitor::GetIceCandidateMonitor(const std::string& id) const
	{
		return FindIn(this->iceCandidates, id);
	}

	IceCandidatePairMonitor* PeerConnectionMonitor::GetIceCandidatePairMonitor(const std::string& id) const
	{
		return FindIn(this->iceCandidatePairs, id);
	}

	CertificateMonitor* PeerConnectionMonitor::GetCertificateMonitor(const std::string& id) const
	{
		return FindIn(this->certificates, id);
	}

	InboundTrackMonitor* PeerConnectionMonitor::GetInboundTrackMonitor(const std::string& trackId) const
	{
		return FindIn(this->inboundTracks, trackId);
	}

	OutboundTrackMonitor* PeerConnectionMonitor::GetOutboundTrackMonitor(const std::string& trackId) const
	{
		return FindIn(this->outboundTracks, trackId);
	}

	bool PeerConnectionMonitor::IsBlockedTransport() const
	{
		for (const auto& [id, transport] : this->iceTransports)
		{
			if (transport->IsBlocked())
			{
				return true;
			}
		}

		return false;
	}

	/* Accepting one stats object per type. */

	void PeerConnectionMonitor::UpdateCodec(const json& raw)
	{
		if (!HasFields(raw, { "id", "timestamp", "payloadType", "mimeType" }))
		{
			CM_WARN("invalid codec stats: %s", raw.dump().c_str());

			return;
		}

		const auto stats = CodecStats::FromJson(raw);
		auto* monitor    = FindIn(this->codecs, stats.id);

		if (!monitor)
		{
			auto created = std::make_unique<CodecMonitor>(this, stats);

			monitor = created.get();
			this->codecs.emplace(stats.id, std::move(created));
			this->parent->Emit(
			  [&](ClientMonitor::Listener* l)
			  {
				  l->OnNewCodecMonitor(this->parent, monitor);
			  });
		}

		monitor->Accept(stats);
	}

	InboundRtpMonitor* PeerConnectionMonitor::UpdateInboundRtp(const json& raw)
	{
		if (!HasFields(raw, { "id", "timestamp", "ssrc", "kind", "trackIdentifier" }))
		{
			CM_WARN("invalid inbound-rtp stats: %s", raw.dump().c_str());

			return nullptr;
		}

		const auto stats = InboundRtpStats::FromJson(raw);
		auto* monitor    = FindIn(this->inboundRtps, stats.ssrc);

		if (!monitor)
		{
			auto created = std::make_unique<InboundRtpMonitor>(this, stats, raw);

			monitor = created.get();
			this->inboundRtps.emplace(stats.ssrc, std::move(created));
			this->parent->Emit(
			  [&](ClientMonitor::Listener* l)
			  {
				  l->OnNewInboundRtpMonitor(this->parent, monitor);
			  });

			auto pending = this->pendingTracks.find(stats.trackIdentifier);

			if (pending != this->pendingTracks.end())
			{
				const auto track       = pending->second.track;
				const auto attachments = pending->second.attachments;

				this->CreateInboundTrackMonitor(track, monitor, attachments);
			}
		}

		monitor->Accept(stats, raw);

		return monitor;
	}

	OutboundRtpMonitor* PeerConnectionMonitor::UpdateOutboundRtp(const json& raw)
	{
		if (!HasFields(raw, { "id", "timestamp", "ssrc", "kind" }))
		{
			CM_WARN("invalid outbound-rtp stats: %s", raw.dump().c_str());

			return nullptr;
		}

		const auto stats = OutboundRtpStats::FromJson(raw);
		auto* monitor    = FindIn(this->outboundRtps, stats.ssrc);

		if (!monitor)
		{
			auto created = std::make_unique<OutboundRtpMonitor>(this, stats);

			monitor = created.get();
			this->outboundRtps.emplace(stats.ssrc, std::move(created));
			this->parent->Emit(
			  [&](ClientMonitor::Listener* l)
			  {
				  l->OnNewOutboundRtpMonitor(this->parent, monitor);
			  });

			if (auto* track = monitor->GetTrack())
			{
				track->outboundRtps[stats.ssrc] = monitor;
			}
		}

		monitor->Accept(stats);

		return monitor;
	}

	RemoteInboundRtpMonitor* PeerConnectionMonitor::UpdateRemoteInboundRtp(const json& raw)
	{
		if (!HasFields(raw, { "id", "timestamp", "ssrc", "kind" }))
		{
			CM_WARN("invalid remote-inbound-rtp stats: %s", raw.dump().c_str());

			return nullptr;
		}

		const auto stats = RemoteInboundRtpStats::FromJson(raw);
		auto* monitor    = FindIn(this->remoteInboundRtps, stats.ssrc);

		if (!monitor)
		{
			auto created = std::make_unique<RemoteInboundRtpMonitor>(this, stats);

			monitor = created.get();
			this->remoteInboundRtps.emplace(stats.ssrc, std::move(created));
			this->parent->Emit(
			  [&](ClientMonitor::Listener* l)
			  {
				  l->OnNewRemoteInboundRtpMonitor(this->parent, monitor);
			  });
		}

		monitor->Accept(stats);

		return monitor;
	}

	RemoteOutboundRtpMonitor* PeerConnectionMonitor::UpdateRemoteOutboundRtp(const json& raw)
	{
		if (!HasFields(raw, { "id", "timestamp", "ssrc", "kind" }))
		{
			CM_WARN("invalid remote-outbound-rtp stats: %s", raw.dump().c_str());

			return nullptr;
		}

		const auto stats = RemoteOutboundRtpStats::FromJson(raw);
		auto* monitor    = FindIn(this->remoteOutboundRtps, stats.ssrc);

		if (!monitor)
		{
			auto created = std::make_unique<RemoteOutboundRtpMonitor>(this, stats);

			monitor = created.get();
			this->remoteOutboundRtps.emplace(stats.ssrc, std::move(created));
			this->parent->Emit(
			  [&](ClientMonitor::Listener* l)
			  {
				  l->OnNewRemoteOutboundRtpMonitor(this->parent, monitor);
			  });
		}

		monitor->Accept(stats);

		return monitor;
	}

	void PeerConnectionMonitor::UpdateMediaSource(const json& raw)
	{
		if (!HasFields(raw, { "id", "timestamp", "trackIdentifier", "kind" }))
		{
			CM_WARN("invalid media-source stats: %s", raw.dump().c_str());

			return;
		}

		const auto stats = MediaSourceStats::FromJson(raw);
		auto* monitor    = FindIn(this->mediaSources, stats.id);

		if (!monitor)
		{
			auto created = std::make_unique<MediaSourceMonitor>(this, stats);

			monitor = created.get();
			this->mediaSources.emplace(stats.id, std::move(created));
			this->parent->Emit(
			  [&](ClientMonitor::Listener* l)
			  {
				  l->OnNewMediaSourceMonitor(this->parent, monitor);
			  });

			auto pending = this->pendingTracks.find(stats.trackIdentifier.value_or(""));

			if (pending != this->pendingTracks.end())
			{
				const auto track       = pending->second.track;
				const auto attachments = pending->second.attachments;

				this->CreateOutboundTrackMonitor(track, monitor, attachments);
			}
		}

		monitor->Accept(stats);
	}

	void PeerConnectionMonitor::UpdateMediaPlayout(const json& raw)
	{
		if (!HasFields(raw, { "id", "timestamp", "kind" }))
		{
			CM_WARN("invalid media-playout stats: %s", raw.dump().c_str());

			return;
		}

		const auto stats = MediaPlayoutStats::FromJson(raw);
		auto* monitor    = FindIn(this->mediaPlayouts, stats.id);

		if (!monitor)
		{
			auto created = std::make_unique<MediaPlayoutMonitor>(this, stats);

			monitor = created.get();
			this->mediaPlayouts.emplace(stats.id, std::move(created));
			this->parent->Emit(
			  [&](ClientMonitor::Listener* l)
			  {
				  l->OnNewMediaPlayoutMonitor(this->parent, monitor);
			  });
		}

		monitor->Accept(stats);
	}

	DataChannelMonitor* PeerConnectionMonitor::UpdateDataChannel(const json& raw)
	{
		if (!HasFields(raw, { "id", "timestamp", "label" }))
		{
			CM_WARN("invalid data-channel stats: %s", raw.dump().c_str());

			return nullptr;
		}

		const auto stats = DataChannelStats::FromJson(raw);
		auto* monitor    = FindIn(this->dataChannels, stats.id);

		if (!monitor)
		{
			auto created = std::make_unique<DataChannelMonitor>(this, stats);

			monitor = created.get();
			this->dataChannels.emplace(stats.id, std::move(created));
			this->parent->Emit(
			  [&](ClientMonitor::Listener* l)
			  {
				  l->OnNewDataChannelMonitor(this->parent, monitor);
			  });
		}

		monitor->Accept(stats);

		return monitor;
	}

	void PeerConnectionMonitor::UpdatePeerConnectionTransport(const json& raw)
	{
		if (!HasFields(raw, { "id", "timestamp", "dataChannelsOpened", "dataChannelsClosed" }))
		{
			CM_WARN("invalid peer-connection stats: %s", raw.dump().c_str());

			return;
		}

		const auto stats = PeerConnectionTransportStats::FromJson(raw);
		auto* monitor    = FindIn(this->peerConnectionTransports, stats.id);

		if (!monitor)
		{
			auto created = std::make_unique<PeerConnectionTransportMonitor>(this, stats);

			monitor = created.get();
			this->peerConnectionTransports.emplace(stats.id, std::move(created));
			this->parent->Emit(
			  [&](ClientMonitor::Listener* l)
			  {
				  l->OnNewPeerConnectionTransportMonitor(this->parent, monitor);
			  });
		}

		monitor->Accept(stats);
	}

	void PeerConnectionMonitor::UpdateIceTransport(const json& raw)
	{
		if (!HasFields(raw, { "id", "timestamp" }))
		{
			CM_WARN("invalid transport stats: %s", raw.dump().c_str());

			return;
		}

		const auto stats = IceTransportStats::FromJson(raw);
		auto* monitor    = FindIn(this->iceTransports, stats.id);

		if (!monitor)
		{
			auto created = std::make_unique<IceTransportMonitor>(this, stats);

			monitor = created.get();
			this->iceTransports.emplace(stats.id, std::move(created));
			this->parent->Emit(
			  [&](ClientMonitor::Listener* l)
			  {
				  l->OnNewIceTransportMonitor(this->parent, monitor);
			  });
		}

		monitor->Accept(stats);
	}

	void PeerConnectionMonitor::UpdateIceCandidate(const json& raw, IceCandidateMonitor::Direction direction)
	{
		if (!HasFields(raw, { "id", "timestamp", "protocol" }))
		{
			CM_WARN("invalid candidate stats: %s", raw.dump().c_str());

			return;
		}

		const auto stats = IceCandidateStats::FromJson(raw);
		auto* monitor    = FindIn(this->iceCandidates, stats.id);

		if (!monitor)
		{
			auto created = std::make_unique<IceCandidateMonitor>(this, stats, direction);

			monitor = created.get();
			this->iceCandidates.emplace(stats.id, std::move(created));
			this->parent->Emit(
			  [&](ClientMonitor::Listener* l)
			  {
				  l->OnNewIceCandidateMonitor(this->parent, monitor);
			  });
		}

		monitor->Accept(stats);
	}

	void PeerConnectionMonitor::UpdateIceCandidatePair(const json& raw)
	{
		if (!HasFields(raw, { "id", "timestamp", "state" }))
		{
			CM_WARN("invalid candidate-pair stats: %s", raw.dump().c_str());

			return;
		}

		const auto stats = IceCandidatePairStats::FromJson(raw);
		auto* monitor    = FindIn(this->iceCandidatePairs, stats.id);

		if (!monitor)
		{
			auto created = std::make_unique<IceCandidatePairMonitor>(this, stats);

			monitor = created.get();
			this->iceCandidatePairs.emplace(stats.id, std::move(created));
			this->parent->Emit(
			  [&](ClientMonitor::Listener* l)
			  {
				  l->OnNewIceCandidatePairMonitor(this->parent, monitor);
			  });
		}

		monitor->Accept(stats);
	}

	void PeerConnectionMonitor::UpdateCertificate(const json& raw)
	{
		if (!HasFields(raw, { "id", "timestamp", "fingerprint", "fingerprintAlgorithm" }))
		{
			CM_WARN("invalid certificate stats: %s", raw.dump().c_str());

			return;
		}

		const auto stats = CertificateStats::FromJson(raw);
		auto* monitor    = FindIn(this->certificates, stats.id);

		if (!monitor)
		{
			auto created = std::make_unique<CertificateMonitor>(this, stats);

			monitor = created.get();
			this->certificates.emplace(stats.id, std::move(created));
			this->parent->Emit(
			  [&](ClientMonitor::Listener* l)
			  {
				  l->OnNewCertificateMonitor(this->parent, monitor);
			  });
		}

		monitor->Accept(stats);
	}

	/* Tracks. */

	void PeerConnectionMonitor::CreateInboundTrackMonitor(
	  const std::shared_ptr<MediaTrack>& track, InboundRtpMonitor* inboundRtp, const json& attachments)
	{
		const auto trackId = track->GetId();

		if (this->inboundTracks.count(trackId))
		{
			return;
		}

		auto created  = std::make_unique<InboundTrackMonitor>(track, inboundRtp, attachments);
		auto* monitor = created.get();

		this->pendingTracks.erase(trackId);
		this->inboundTracks.emplace(trackId, std::move(created));

		if (auto context = this->parent->TakePendingInboundTrackContext(trackId))
		{
			monitor->SetContext(*context);
		}

		this->parent->Emit(
		  [&](ClientMonitor::Listener* l)
		  {
			  l->OnNewInboundTrackMonitor(this->parent, monitor);
		  });
	}

	void PeerConnectionMonitor::CreateOutboundTrackMonitor(
	  const std::shared_ptr<MediaTrack>& track, MediaSourceMonitor* mediaSource, const json& attachments)
	{
		const auto trackId = track->GetId();

		if (this->outboundTracks.count(trackId))
		{
			return;
		}

		auto created  = std::make_unique<OutboundTrackMonitor>(track, mediaSource, attachments);
		auto* monitor = created.get();

		this->pendingTracks.erase(trackId);
		this->outboundTracks.emplace(trackId, std::move(created));

		for (const auto& [ssrc, outboundRtp] : this->outboundRtps)
		{
			if (outboundRtp->GetTrackIdentifier() == trackId)
			{
				monitor->outboundRtps[ssrc] = outboundRtp.get();
			}
		}

		if (auto context = this->parent->TakePendingOutboundTrackContext(trackId))
		{
			monitor->SetContext(*context);
		}

		// Tells the server which device the outbound audio track captures from.
		if (track->GetKind() == "audio")
		{
			json payload = {
				{ "peerConnectionId", this->peerConnectionId },
				{ "trackId",          trackId                },
				{ "label",            track->GetLabel()      }
			};

			if (const auto& settings = monitor->GetSettings())
			{
				if (settings->deviceId.has_value() && !settings->deviceId->empty())
				{
					payload["deviceId"] = *settings->deviceId;
				}
				if (settings->groupId.has_value() && !settings->groupId->empty())
				{
					payload["groupId"] = *settings->groupId;
				}
			}

			this->parent->AddMetaData(ClientMetaTypes::AUDIO_INPUT_DEVICE, payload);
		}

		this->parent->Emit(
		  [&](ClientMonitor::Listener* l)
		  {
			  l->OnNewOutboundTrackMonitor(this->parent, monitor);
		  });
	}

	/* Aggregates. */

	void PeerConnectionMonitor::UpdatePeerConnectionLimitationReason(
	  const std::optional<std::string>& current)
	{
		if (!current.has_value())
		{
			return;
		}

		const auto& priority = LimitationPriority();
		auto next            = priority.find(*current);

		if (next == priority.end())
		{
			return;
		}
		if (!this->metrics.qualityLimitationReason.has_value())
		{
			this->metrics.qualityLimitationReason = current;

			return;
		}
		if (priority.at(*this->metrics.qualityLimitationReason) < next->second)
		{
			this->metrics.qualityLimitationReason = current;
		}
	}

	void PeerConnectionMonitor::UpdateTransportQualityAverages()
	{
		// Averaged over the streams that carried media; ones that received nothing are excluded.
		double lossSum{ 0 };
		double lossCount{ 0 };
		double jitterSum{ 0 };
		double jitterCount{ 0 };

		for (const auto& [ssrc, rtp] : this->inboundRtps)
		{
			if (rtp->GetMetrics().deltaPacketsReceived.value_or(0) == 0)
			{
				continue;
			}
			if (rtp->GetMetrics().deltaFractionLost.has_value())
			{
				lossSum += *rtp->GetMetrics().deltaFractionLost;
				lossCount += 1;
			}
			if (rtp->GetStats().jitter.has_value())
			{
				jitterSum += *rtp->GetStats().jitter * 1000;
				jitterCount += 1;
			}
		}

		this->metrics.avgInboundFractionLost =
		  0 < lossCount ? std::optional<double>(lossSum / lossCount) : std::nullopt;
		this->metrics.avgInboundJitterInMs =
		  0 < jitterCount ? std::optional<double>(jitterSum / jitterCount) : std::nullopt;

		double outboundLossSum{ 0 };
		double outboundLossCount{ 0 };

		for (const auto& [ssrc, remote] : this->remoteInboundRtps)
		{
			if (!remote->GetMetrics().deltaFractionLost.has_value())
			{
				continue;
			}

			outboundLossSum += *remote->GetMetrics().deltaFractionLost;
			outboundLossCount += 1;
		}

		this->metrics.avgOutboundFractionLost =
		  0 < outboundLossCount ? std::optional<double>(outboundLossSum / outboundLossCount)
		                        : std::nullopt;
	}

	void PeerConnectionMonitor::RefreshTransportStability()
	{
		const auto rttInSec     = this->GetAvgRttInSec();
		const auto jitterInMs   = this->metrics.avgInboundJitterInMs;
		const auto lossFraction = this->metrics.avgInboundFractionLost.has_value()
		                            ? this->metrics.avgInboundFractionLost
		                            : this->metrics.avgOutboundFractionLost;

		if (!rttInSec.has_value() || !jitterInMs.has_value() || !lossFraction.has_value())
		{
			this->metrics.transportStability.reset();

			return;
		}

		// The E-model's impairment term expects percent, not a fraction.
		this->metrics.transportStability = utils::TransportStability(
		  *rttInSec * 1000,
		  *jitterInMs,
		  std::max(
		    this->metrics.avgInboundFractionLost.value_or(0),
		    this->metrics.avgOutboundFractionLost.value_or(0)) *
		    100);
	}

	void PeerConnectionMonitor::FeedSlicedWindow()
	{
		// The raw W3C totals go in rather than per-collection deltas, so a delta spans exactly the
		// stretch its duration measures. A report with a total but no count contributes to neither.
		std::optional<double> rtcpTimeInMs;
		std::optional<double> rtcpMeasurements;
		std::optional<double> iceTimeInMs;
		std::optional<double> iceResponses;

		for (const auto& [ssrc, remote] : this->remoteInboundRtps)
		{
			const auto& s = remote->GetStats();

			if (!s.totalRoundTripTime.has_value() || !s.roundTripTimeMeasurements.has_value())
			{
				continue;
			}

			rtcpTimeInMs = rtcpTimeInMs.value_or(0) + (*s.totalRoundTripTime * 1000);
			rtcpMeasurements =
			  rtcpMeasurements.value_or(0) + static_cast<double>(*s.roundTripTimeMeasurements);
		}
		for (const auto& [ssrc, remote] : this->remoteOutboundRtps)
		{
			const auto& s = remote->GetStats();

			if (!s.totalRoundTripTime.has_value() || !s.roundTripTimeMeasurements.has_value())
			{
				continue;
			}

			rtcpTimeInMs = rtcpTimeInMs.value_or(0) + (*s.totalRoundTripTime * 1000);
			rtcpMeasurements =
			  rtcpMeasurements.value_or(0) + static_cast<double>(*s.roundTripTimeMeasurements);
		}
		for (auto* pair : this->GetSelectedIceCandidatePairs())
		{
			const auto& s = pair->GetStats();

			if (!s.totalRoundTripTime.has_value() || !s.responsesReceived.has_value())
			{
				continue;
			}

			iceTimeInMs  = iceTimeInMs.value_or(0) + (*s.totalRoundTripTime * 1000);
			iceResponses = iceResponses.value_or(0) + static_cast<double>(*s.responsesReceived);
		}

		SlicedWindow::Values values(WINDOW_TOTALS_COUNT);

		values[TOTAL_RTCP_ROUND_TRIP_TIME_IN_MS]   = rtcpTimeInMs;
		values[TOTAL_RTCP_ROUND_TRIP_MEASUREMENTS] = rtcpMeasurements;
		values[TOTAL_ICE_ROUND_TRIP_TIME_IN_MS]    = iceTimeInMs;
		values[TOTAL_ICE_RESPONSES_RECEIVED]       = iceResponses;

		try
		{
			this->slicedWindow->Add(this->metrics.statsClockTime, values);
		}
		catch (const std::exception& error)
		{
			CM_WARN("failed to feed the sliced window: %s", error.what());
		}
	}

	std::optional<std::string> PeerConnectionMonitor::MostSevereIceState() const
	{
		const auto& severity = IceStateSeverity();
		std::optional<std::string> worst;

		const auto rank = [&severity](const std::string& state)
		{
			auto it = severity.find(state);

			return it == severity.end() ? -1 : it->second;
		};

		for (const auto& [id, transport] : this->iceTransports)
		{
			const auto& state = transport->GetStats().iceState;

			if (!state.has_value())
			{
				continue;
			}
			if (!worst.has_value() || rank(*worst) < rank(*state))
			{
				worst = state;
			}
		}

		return worst;
	}

	bool PeerConnectionMonitor::HasLiveTrack(const std::optional<std::string>& trackIdentifier) const
	{
		// A stats entry that stops being reported is not on its own evidence that anything went
		// away: a busy client drops a round now and then. The track is the evidence.
		if (!trackIdentifier.has_value())
		{
			return false;
		}

		std::shared_ptr<MediaTrack> track;

		if (auto* inbound = this->GetInboundTrackMonitor(*trackIdentifier))
		{
			track = inbound->GetTrack();
		}
		else if (auto* outbound = this->GetOutboundTrackMonitor(*trackIdentifier))
		{
			track = outbound->GetTrack();
		}
		else
		{
			auto it = this->pendingTracks.find(*trackIdentifier);

			if (it != this->pendingTracks.end())
			{
				track = it->second.track;
			}
		}

		return track && track->GetReadyState() == MediaTrack::ReadyState::LIVE;
	}

	void PeerConnectionMonitor::CheckVisited()
	{
		for (auto it = this->codecs.begin(); it != this->codecs.end();)
		{
			it = it->second->ConsumeVisited() ? std::next(it) : this->codecs.erase(it);
		}

		for (auto it = this->inboundRtps.begin(); it != this->inboundRtps.end();)
		{
			auto* monitor = it->second.get();

			if (monitor->ConsumeVisited())
			{
				++it;

				continue;
			}
			// The rtp monitor outlives a round that did not report it, so long as its track is
			// still live: dropping it would cost a live track its monitor for good.
			if (!this->closed && this->HasLiveTrack(monitor->GetTrackIdentifier()))
			{
				++it;

				continue;
			}

			auto track = this->inboundTracks.find(monitor->GetTrackIdentifier());

			if (track != this->inboundTracks.end())
			{
				track->second->GetIssues().ResolveAll("the track stopped being reported");
				this->inboundTracks.erase(track);
			}

			it = this->inboundRtps.erase(it);
		}

		for (auto it = this->remoteOutboundRtps.begin(); it != this->remoteOutboundRtps.end();)
		{
			it = it->second->ConsumeVisited() ? std::next(it) : this->remoteOutboundRtps.erase(it);
		}

		for (auto it = this->outboundRtps.begin(); it != this->outboundRtps.end();)
		{
			auto* monitor = it->second.get();

			if (monitor->ConsumeVisited())
			{
				++it;

				continue;
			}
			if (!this->closed && this->HasLiveTrack(monitor->GetTrackIdentifier()))
			{
				++it;

				continue;
			}

			for (auto& [trackId, track] : this->outboundTracks)
			{
				track->outboundRtps.erase(monitor->GetSsrc());

				if (track->highestLayer == monitor)
				{
					track->highestLayer = nullptr;
				}
			}

			it = this->outboundRtps.erase(it);
		}

		for (auto it = this->remoteInboundRtps.begin(); it != this->remoteInboundRtps.end();)
		{
			it = it->second->ConsumeVisited() ? std::next(it) : this->remoteInboundRtps.erase(it);
		}

		for (auto it = this->mediaSources.begin(); it != this->mediaSources.end();)
		{
			auto* monitor = it->second.get();

			if (monitor->ConsumeVisited())
			{
				++it;

				continue;
			}
			if (!this->closed && this->HasLiveTrack(monitor->GetStats().trackIdentifier))
			{
				++it;

				continue;
			}

			auto track = this->outboundTracks.find(monitor->GetStats().trackIdentifier.value_or(""));

			if (track != this->outboundTracks.end())
			{
				// A source that went away takes its stats entry with it, so the ordinary update
				// pass would never see the track again; its detectors get their last look here.
				if (track->second->IsSourceEnded())
				{
					track->second->GetDetectors().Update();
				}

				track->second->GetIssues().ResolveAll("the track stopped being reported");
				this->outboundTracks.erase(track);
			}

			it = this->mediaSources.erase(it);
		}

		for (auto it = this->mediaPlayouts.begin(); it != this->mediaPlayouts.end();)
		{
			it = it->second->ConsumeVisited() ? std::next(it) : this->mediaPlayouts.erase(it);
		}
		for (auto it = this->peerConnectionTransports.begin();
		     it != this->peerConnectionTransports.end();)
		{
			it = it->second->ConsumeVisited() ? std::next(it) : this->peerConnectionTransports.erase(it);
		}
		for (auto it = this->iceTransports.begin(); it != this->iceTransports.end();)
		{
			if (it->second->ConsumeVisited())
			{
				++it;

				continue;
			}

			it->second->GetIssues().ResolveAll("the ice transport stopped being reported");
			it = this->iceTransports.erase(it);
		}
		for (auto it = this->iceCandidates.begin(); it != this->iceCandidates.end();)
		{
			it = it->second->ConsumeVisited() ? std::next(it) : this->iceCandidates.erase(it);
		}
		for (auto it = this->iceCandidatePairs.begin(); it != this->iceCandidatePairs.end();)
		{
			it = it->second->ConsumeVisited() ? std::next(it) : this->iceCandidatePairs.erase(it);
		}
		for (auto it = this->certificates.begin(); it != this->certificates.end();)
		{
			it = it->second->ConsumeVisited() ? std::next(it) : this->certificates.erase(it);
		}
		for (auto it = this->dataChannels.begin(); it != this->dataChannels.end();)
		{
			it = it->second->ConsumeVisited() ? std::next(it) : this->dataChannels.erase(it);
		}

		// Without this, a track that ended before it was ever reported stays parked for good.
		for (auto it = this->pendingTracks.begin(); it != this->pendingTracks.end();)
		{
			it = it->second.track->GetReadyState() == MediaTrack::ReadyState::LIVE
			       ? std::next(it)
			       : this->pendingTracks.erase(it);
		}

		// Ended outbound tracks whose source is still reported: dropped after their last look.
		for (auto it = this->outboundTracks.begin(); it != this->outboundTracks.end();)
		{
			if (!it->second->IsSourceEnded())
			{
				++it;

				continue;
			}

			it->second->GetDetectors().Update();
			it->second->GetIssues().ResolveAll("the track ended");
			it = this->outboundTracks.erase(it);
		}
	}
} // namespace clientmonitor
