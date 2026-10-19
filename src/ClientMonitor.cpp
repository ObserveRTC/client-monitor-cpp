#define CM_CLASS "ClientMonitor"

#include "clientmonitor/ClientMonitor.hpp"
#include "clientmonitor/utils/JsonReader.hpp"
#include "clientmonitor/utils/MediaMath.hpp"
#include "clientmonitor/utils/Utils.hpp"
#include "clientmonitor/Logger.hpp"
#include "clientmonitor/detectors/StatsGapDetector.hpp"
#include "clientmonitor/schema/ClientEventTypes.hpp"

#include <algorithm>

using json = nlohmann::json;

namespace observertc::clientmonitor
{
	ClientMonitor::ClientMonitor(const ClientMonitorConfig& config, Listener* listener)
	  : config(config), createdAt(utils::NowMs()), activeIssues(this)
	{
		CM_TRACE();

		if (listener)
		{
			this->listeners.push_back(listener);
		}

		this->slicedWindow = std::make_unique<SlicedWindow>(
		  this->CreateWindowConfig(this->config.clientWindow, WINDOW_TOTALS_COUNT));

		// Native stats are Chromium's; the adapter is a no-op on an already standard report.
		this->statsAdapters.push_back(std::make_unique<LibWebRtcStatsAdapter>());

		if (this->config.bufferClientSamplesUntilSubscriber)
		{
			this->bufferedClientSamples.emplace();
		}

		if (this->config.statsGapDetector.has_value())
		{
			this->detectors.Add(std::make_unique<StatsGapDetector>(this));
		}

		// CpuPerformanceDetector registers here once ported.

		this->SetSamplingTick();

		if (this->config.addClientJointEventOnCreated)
		{
			this->AddClientJoinEvent();
		}

		// Last, so the timer never sees a half-built monitor.
		this->SetCollectingPeriod(this->config.collectingPeriodInMs);
	}

	ClientMonitor::~ClientMonitor()
	{
		CM_TRACE();

		this->Close();
		this->timer.Stop();
	}

	/* Listeners. */

	void ClientMonitor::AddListener(Listener* listener)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		if (
		  !listener ||
		  std::find(this->listeners.begin(), this->listeners.end(), listener) != this->listeners.end())
		{
			return;
		}

		this->listeners.push_back(listener);
		this->FlushBufferedClientSamples();
	}

	void ClientMonitor::RemoveListener(Listener* listener)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		this->listeners.erase(
		  std::remove(this->listeners.begin(), this->listeners.end(), listener), this->listeners.end());
	}

	/* Identity. */

	std::optional<std::string> ClientMonitor::GetClientId() const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		return this->config.clientId;
	}

	void ClientMonitor::SetClientId(const std::optional<std::string>& clientId)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		this->config.clientId = clientId;
	}

	std::optional<std::string> ClientMonitor::GetCallId() const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		return this->config.callId;
	}

	void ClientMonitor::SetCallId(const std::optional<std::string>& callId)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		this->config.callId = callId;
	}

	void ClientMonitor::SetAttachments(const json& attachments)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		this->attachments = attachments;
	}

	void ClientMonitor::UpdateAttachments(const json& patch)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		this->attachments.merge_patch(patch);
	}

	json ClientMonitor::GetAttachments() const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		return this->attachments;
	}

	int64_t ClientMonitor::GetUptimeInMs() const
	{
		return utils::NowMs() - this->createdAt;
	}

	bool ClientMonitor::IsClosed() const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		return this->closed;
	}

	void ClientMonitor::Close()
	{
		CM_TRACE();

		{
			std::lock_guard<std::recursive_mutex> lock(this->mutex);

			if (this->closed)
			{
				return;
			}

			// Auto-resolve stateful issues before `closed`, so consumers see a clean lifecycle.
			for (const auto& key : this->activeIssues.GetKeys())
			{
				this->ResolveIssue(key, std::string("monitor closed before issue could be resolved"));
			}

			if (this->config.addClientLeftEventOnClose)
			{
				this->AddClientLeftEvent();
			}
			if (0 < this->samplingTick)
			{
				// The last sample before close.
				this->CreateSample();
			}
			if (this->bufferedClientSamples.has_value() && !this->bufferedClientSamples->empty())
			{
				CM_WARN(
				  "closing with %zu buffered sample(s) never delivered, because no listener was ever added",
				  this->bufferedClientSamples->size());
			}

			this->bufferedClientSamples.reset();
			this->closed = true;

			for (auto& [id, peerConnection] : this->peerConnections)
			{
				peerConnection->Close();
			}

			this->Emit(
			  [this](Listener* listener)
			  {
				  listener->OnClose(this);
			  });
		}

		// Outside the lock: a collection in flight on the timer thread may be waiting for it.
		this->timer.Stop();
	}

	/* Collection and sampling. */

	void ClientMonitor::AddStatsAdapter(std::unique_ptr<StatsAdapter> adapter)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

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

	bool ClientMonitor::RemoveStatsAdapter(const std::string& name)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

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

	StatsAdapter* ClientMonitor::GetStatsAdapter(const std::string& name) const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		for (const auto& adapter : this->statsAdapters)
		{
			if (adapter->GetName() == name)
			{
				return adapter.get();
			}
		}

		return nullptr;
	}

	json ClientMonitor::AdaptAndAccept(PeerConnectionMonitor& peerConnection, json rawStats)
	{
		const auto& peerConnectionId = peerConnection.GetPeerConnectionId();

		this->Emit(
		  [&](Listener* listener)
		  {
			  listener->OnRawStats(this, peerConnectionId, rawStats);
		  });

		for (auto& adapter : this->statsAdapters)
		{
			try
			{
				adapter->Adapt(peerConnectionId, rawStats);
			}
			catch (const std::exception& error)
			{
				CM_WARN("error adapting stats with %s: %s", adapter->GetName().c_str(), error.what());
			}
		}

		peerConnection.AcceptAdaptedStats(rawStats);

		return rawStats;
	}

	void ClientMonitor::AcceptStats(const std::string& peerConnectionId, json rawStats)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		auto it = this->peerConnections.find(peerConnectionId);

		if (this->closed || it == this->peerConnections.end() || it->second->IsClosed())
		{
			return;
		}

		this->AdaptAndAccept(*it->second, std::move(rawStats));
	}

	ClientMonitor::Listener::StatsCollectedEvent ClientMonitor::Collect()
	{
		CM_TRACE();

		Listener::StatsCollectedEvent event;
		std::vector<std::shared_ptr<PeerConnectionMonitor>> peerConnections;
		std::vector<ExtensionStatsProvider> providers;

		{
			std::lock_guard<std::recursive_mutex> lock(this->mutex);

			if (this->closed)
			{
				CM_WARN("ClientMonitor is closed, cannot collect stats");

				return event;
			}

			this->DropClosedPeerConnections();
			this->lastCollectingStatsAt = utils::NowMs();
			event.startedAt             = this->lastCollectingStatsAt;

			for (auto& [id, peerConnection] : this->peerConnections)
			{
				peerConnections.push_back(peerConnection);
			}
			for (auto& [handle, provider] : this->extensionStatsProviders)
			{
				providers.push_back(provider);
			}
		}

		// Without the lock: a collector may block on the thread that owns the peer connection.
		std::vector<std::pair<std::shared_ptr<PeerConnectionMonitor>, nlohmann::json>> rawReports;

		for (auto& peerConnection : peerConnections)
		{
			try
			{
				rawReports.emplace_back(peerConnection, peerConnection->GetRawStats());
			}
			catch (const std::exception& error)
			{
				CM_ERROR(
				  "failed to get stats from peer connection %s: %s",
				  peerConnection->GetPeerConnectionId().c_str(),
				  error.what());
			}
		}

		// Adapted to W3C shape first; only then do the peer connection monitors read them.
		{
			std::lock_guard<std::recursive_mutex> lock(this->mutex);

			if (this->closed)
			{
				return event;
			}

			for (auto& [peerConnection, rawStats] : rawReports)
			{
				if (peerConnection->IsClosed())
				{
					continue;
				}

				event.collectedStats.emplace_back(
				  peerConnection->GetPeerConnectionId(), this->AdaptAndAccept(*peerConnection, std::move(rawStats)));
			}
		}

		std::vector<ExtensionStatsInput> extensionStatsInputs;

		for (auto& provider : providers)
		{
			try
			{
				if (auto stats = provider())
				{
					extensionStatsInputs.push_back(std::move(*stats));
				}
			}
			catch (const std::exception& error)
			{
				CM_ERROR("failed to get extension stats: %s", error.what());
			}
		}

		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		if (this->closed)
		{
			return event;
		}

		for (const auto& stats : extensionStatsInputs)
		{
			this->AddExtensionStats(stats);
		}

		auto& m = this->metrics;

		m.sendingAudioBitrate           = 0;
		m.sendingVideoBitrate           = 0;
		m.receivingAudioBitrate         = 0;
		m.receivingVideoBitrate         = 0;
		m.totalAvailableIncomingBitrate = 0;
		m.totalAvailableOutgoingBitrate = 0;

		double rttSum{ 0 };
		size_t count{ 0 };
		std::optional<double> totalVideoEncodeTimeInMs;
		std::optional<double> totalVideoDecodeTimeInMs;

		for (auto* peerConnection : this->GetPeerConnections())
		{
			const auto& pm = peerConnection->GetMetrics();

			m.sendingAudioBitrate += pm.sendingAudioBitrate;
			m.sendingVideoBitrate += pm.sendingVideoBitrate;
			m.receivingAudioBitrate += pm.receivingAudioBitrate;
			m.receivingVideoBitrate += pm.receivingVideoBitrate;
			m.totalAvailableIncomingBitrate += pm.totalAvailableIncomingBitrate.value_or(0);
			m.totalAvailableOutgoingBitrate += pm.totalAvailableOutgoingBitrate.value_or(0);
			rttSum += peerConnection->GetAvgRttInSec().value_or(0);
			totalVideoEncodeTimeInMs =
			  utils::AccumulatedValue(totalVideoEncodeTimeInMs, pm.totalVideoEncodeTimeInMs);
			totalVideoDecodeTimeInMs =
			  utils::AccumulatedValue(totalVideoDecodeTimeInMs, pm.totalVideoDecodeTimeInMs);
			++count;
		}

		// Guarded against no peer connections producing NaN.
		m.avgRttInSec                       = 0 < count ? rttSum / static_cast<double>(count) : -1;
		m.durationOfCollectingStatsInMs     = utils::NowMs() - this->lastCollectingStatsAt;
		event.durationOfCollectingStatsInMs = m.durationOfCollectingStatsInMs;

		SlicedWindow::Values values(WINDOW_TOTALS_COUNT);

		values[TOTAL_VIDEO_ENCODE_TIME_IN_MS] = totalVideoEncodeTimeInMs;
		values[TOTAL_VIDEO_DECODE_TIME_IN_MS] = totalVideoDecodeTimeInMs;

		try
		{
			this->slicedWindow->Add(static_cast<double>(utils::NowMs()), values);
		}
		catch (const std::exception& error)
		{
			CM_WARN("failed to feed the client window: %s", error.what());
		}

		for (auto* track : this->GetInboundTracks())
		{
			track->Update();
		}
		for (auto* track : this->GetOutboundTracks())
		{
			track->Update();
		}

		this->detectors.Update();

		if (this->scoreCalculator)
		{
			try
			{
				this->scoreCalculator->Update(this);
			}
			catch (const std::exception& error)
			{
				CM_WARN("score calculator failed: %s", error.what());
			}
		}

		// An id-keyed extension stat survives one collection without being reported.
		for (auto it = this->extensionStatsEntries.begin(); it != this->extensionStatsEntries.end();)
		{
			if (it->second.visited)
			{
				it->second.visited = false;
				++it;

				continue;
			}

			it = this->extensionStatsEntries.erase(it);
		}

		this->Emit(
		  [&](Listener* listener)
		  {
			  listener->OnStatsCollected(this, event);
		  });

		if (0 < this->samplingTick)
		{
			if (++this->collectingCounter % this->samplingTick == 0)
			{
				this->CreateSample();
			}
		}

		return event;
	}

	std::optional<ClientSample> ClientMonitor::CreateSample()
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		if (this->closed)
		{
			return std::nullopt;
		}

		ClientSample sample;

		sample.clientId    = this->config.clientId;
		sample.callId      = this->config.callId;
		sample.timestamp   = static_cast<double>(utils::NowMs());
		sample.attachments = this->attachments;
		sample.score       = this->score;
		// The client's own reasons only; the aggregate is on OnScore().
		sample.scoreReasons =
		  utils::SampledScoreReasons(this->scoreReasons, this->config.sendScoreReasonsToServer);

		sample.peerConnections.emplace();

		for (auto* peerConnection : this->GetPeerConnections())
		{
			sample.peerConnections->push_back(peerConnection->CreateSample());
		}

		sample.clientEvents    = std::move(this->clientEvents);
		sample.clientMetaItems = std::move(this->clientMetaItems);
		sample.clientIssues    = std::move(this->clientIssues);
		sample.extensionStats  = std::move(this->extensionStats);

		this->clientEvents.clear();
		this->clientMetaItems.clear();
		this->clientIssues.clear();
		this->extensionStats.clear();

		this->lastSampledAt = utils::NowMs();

		if (this->bufferedClientSamples.has_value())
		{
			if (this->listeners.empty())
			{
				this->bufferedClientSamples->push_back(sample);
			}
			else
			{
				this->FlushBufferedClientSamples();
			}
		}

		this->Emit(
		  [&](Listener* listener)
		  {
			  listener->OnSampleCreated(this, sample);
		  });

		return sample;
	}

	void ClientMonitor::SetCollectingPeriod(int64_t collectingPeriodInMs)
	{
		{
			std::lock_guard<std::recursive_mutex> lock(this->mutex);

			if (this->closed)
			{
				return;
			}

			this->config.collectingPeriodInMs = collectingPeriodInMs;
			this->SetSamplingTick();
		}

		// Outside the lock: stopping a running timer waits for a collection in flight.
		if (collectingPeriodInMs <= 0)
		{
			this->timer.Stop();

			return;
		}

		this->timer.Start(
		  collectingPeriodInMs,
		  [this]()
		  {
			  try
			  {
				  this->Collect();
			  }
			  catch (const std::exception& error)
			  {
				  CM_ERROR("collection failed: %s", error.what());
			  }
		  });
	}

	void ClientMonitor::SetSamplingPeriod(int64_t samplingPeriodInMs)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		this->config.samplingPeriodInMs = samplingPeriodInMs;
		this->SetSamplingTick();
	}

	int64_t ClientMonitor::GetLastSampledAt() const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		return this->lastSampledAt;
	}

	int64_t ClientMonitor::GetLastCollectingStatsAt() const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		return this->lastCollectingStatsAt;
	}

	/* Sources. */

	PeerConnectionMonitor* ClientMonitor::AddPeerConnection(
	  std::shared_ptr<StatsCollector> statsCollector,
	  const std::string& peerConnectionId,
	  const json& attachments)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		if (this->closed)
		{
			CM_WARN("cannot add a peer connection to a closed ClientMonitor");

			return nullptr;
		}

		const auto id = peerConnectionId.empty() ? utils::CreateUuid() : peerConnectionId;

		if (this->peerConnections.count(id))
		{
			CM_WARN("PeerConnectionMonitor with id %s already exists", id.c_str());

			return nullptr;
		}

		auto peerConnection =
		  std::make_shared<PeerConnectionMonitor>(this, id, std::move(statsCollector), attachments);
		auto* raw = peerConnection.get();

		this->peerConnections.emplace(id, std::move(peerConnection));
		this->Emit(
		  [&](Listener* listener)
		  {
			  listener->OnNewPeerConnectionMonitor(this, raw);
		  });

		return raw;
	}

	bool ClientMonitor::RemovePeerConnection(const std::string& peerConnectionId)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		auto it = this->peerConnections.find(peerConnectionId);

		if (it == this->peerConnections.end())
		{
			return false;
		}

		it->second->Close();
		this->peerConnections.erase(it);

		return true;
	}

	/* Events, issues, meta. */

	void ClientMonitor::AddEvent(
	  const std::string& type, const json& payload, std::optional<int64_t> timestamp)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		if (this->closed || !this->IsBuffering())
		{
			return;
		}

		ClientEvent event;

		event.type      = type;
		event.payload   = payload;
		event.timestamp = timestamp.value_or(utils::NowMs());

		this->clientEvents.push_back(event);
		this->Emit(
		  [&](Listener* listener)
		  {
			  listener->OnClientEvent(this, event);
		  });
	}

	void ClientMonitor::AddClientJoinEvent(const json& payload, std::optional<int64_t> timestamp)
	{
		this->AddEvent(
		  ClientEventTypes::CLIENT_JOINED, payload.is_null() ? json::object() : payload, timestamp);
	}

	void ClientMonitor::AddClientLeftEvent(const json& payload, std::optional<int64_t> timestamp)
	{
		this->AddEvent(
		  ClientEventTypes::CLIENT_LEFT, payload.is_null() ? json::object() : payload, timestamp);
	}

	std::optional<AddedClientIssue> ClientMonitor::AddIssue(
	  const std::string& type, const json& payload, std::optional<int64_t> timestamp, bool includeInSample)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		if (this->closed)
		{
			return std::nullopt;
		}

		AddedClientIssue issue;

		issue.type            = type;
		issue.payload         = payload;
		issue.timestamp       = timestamp.value_or(utils::NowMs());
		issue.includeInSample = includeInSample;

		if (includeInSample)
		{
			this->BufferIssueForSample(issue.type, issue.payload, issue.timestamp);
		}

		const auto reported = ReportedClientIssue::From(issue);

		this->Emit(
		  [&](Listener* listener)
		  {
			  listener->OnIssue(this, reported);
		  });

		return issue;
	}

	const RaisedClientIssue* ClientMonitor::RaiseIssue(
	  const std::string& key,
	  const std::string& type,
	  const json& payload,
	  std::optional<int64_t> timestamp,
	  bool includeInSample)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		if (this->closed)
		{
			return nullptr;
		}

		if (!this->activeIssues.Has(key))
		{
			this->activeIssues.Raise({ key, type, payload, timestamp, includeInSample });
		}
		else
		{
			this->activeIssues.Update({ key, payload, timestamp, includeInSample });
		}

		return this->activeIssues.Get(key);
	}

	std::optional<ResolvedClientIssue> ClientMonitor::ResolveIssue(
	  const std::string& key,
	  const std::optional<std::string>& comment,
	  const json& payload,
	  std::optional<int64_t> resolvedAt)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		if (this->closed)
		{
			return std::nullopt;
		}

		return this->activeIssues.Resolve({ key, comment, payload, resolvedAt });
	}

	bool ClientMonitor::IsIssueActive(const std::string& key) const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		return this->activeIssues.Has(key);
	}

	std::vector<RaisedClientIssue> ClientMonitor::GetActiveIssuesByType(const std::string& type) const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		std::vector<RaisedClientIssue> result;

		for (const auto* issue : this->activeIssues.GetByType(type))
		{
			result.push_back(*issue);
		}

		return result;
	}

	void ClientMonitor::AddMetaData(
	  const std::string& type, const json& payload, std::optional<int64_t> timestamp)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		if (this->closed || !this->IsBuffering())
		{
			return;
		}

		ClientMetaData metaData;

		metaData.type      = type;
		metaData.payload   = payload;
		metaData.timestamp = timestamp.value_or(utils::NowMs());

		if (payload.is_object())
		{
			if (auto id = detail::ReadString(payload, "peerConnectionId"))
			{
				metaData.peerConnectionId = id;
			}
			if (auto id = detail::ReadString(payload, "trackId"))
			{
				metaData.trackId = id;
			}
		}

		this->AddMetaData(std::move(metaData));
	}

	void ClientMonitor::AddMetaData(ClientMetaData metaData)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		if (this->closed || !this->IsBuffering())
		{
			return;
		}
		if (!metaData.timestamp)
		{
			metaData.timestamp = utils::NowMs();
		}

		this->clientMetaItems.push_back(metaData);
		this->Emit(
		  [&](Listener* listener)
		  {
			  listener->OnMeta(this, metaData);
		  });
	}

	/* Extension stats. */

	void ClientMonitor::AddExtensionStats(const ExtensionStatsInput& stats)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		if (this->closed)
		{
			return;
		}

		// Kept whether or not sampling is on: reading your own metrics back has nothing to do with
		// whether samples are being produced.
		if (stats.id.has_value())
		{
			auto& entry = this->extensionStatsEntries[*stats.id];

			entry.id        = *stats.id;
			entry.type      = stats.type;
			entry.payload   = stats.payload;
			entry.timestamp = utils::NowMs();
			entry.visited   = true;
		}

		if (!this->IsBuffering())
		{
			return;
		}

		ExtensionStat stat;

		stat.type    = stats.type;
		stat.payload = stats.payload;

		this->extensionStats.push_back(stat);
		this->Emit(
		  [&](Listener* listener)
		  {
			  listener->OnExtensionStats(this, stat);
		  });
	}

	uint64_t ClientMonitor::AddExtensionStatsProvider(ExtensionStatsProvider provider)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		const auto handle = this->nextProviderHandle++;

		this->extensionStatsProviders.emplace(handle, std::move(provider));

		return handle;
	}

	bool ClientMonitor::RemoveExtensionStatsProvider(uint64_t handle)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		return this->extensionStatsProviders.erase(handle) > 0;
	}

	std::optional<json> ClientMonitor::GetExtensionStatsPayload(const std::string& id) const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		auto it = this->extensionStatsEntries.find(id);

		if (it == this->extensionStatsEntries.end())
		{
			return std::nullopt;
		}

		return it->second.payload;
	}

	std::optional<ClientMonitor::ExtensionStatsEntry> ClientMonitor::GetExtensionStatsEntry(
	  const std::string& id) const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		auto it = this->extensionStatsEntries.find(id);

		if (it == this->extensionStatsEntries.end())
		{
			return std::nullopt;
		}

		return it->second;
	}

	/* Score. */

	void ClientMonitor::SetScore(
	  double score,
	  const std::map<std::string, double>& ownReasons,
	  const std::map<std::string, double>& aggregatedReasons)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		if (this->closed)
		{
			return;
		}

		this->score        = score;
		this->scoreReasons = ownReasons;

		const auto& reasons = aggregatedReasons.empty() ? ownReasons : aggregatedReasons;

		this->Emit(
		  [&](Listener* listener)
		  {
			  listener->OnScore(this, score, reasons);
		  });
	}

	double ClientMonitor::GetScore() const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		return this->score;
	}

	std::map<std::string, double> ClientMonitor::GetScoreReasons() const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		return this->scoreReasons;
	}

	void ClientMonitor::SetScoreCalculator(std::unique_ptr<ScoreCalculator> scoreCalculator)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		this->scoreCalculator = std::move(scoreCalculator);
	}

	/* The graph. */

	PeerConnectionMonitor* ClientMonitor::GetPeerConnectionMonitor(const std::string& peerConnectionId) const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		auto it = this->peerConnections.find(peerConnectionId);

		return it == this->peerConnections.end() || it->second->IsClosed() ? nullptr : it->second.get();
	}

	std::vector<PeerConnectionMonitor*> ClientMonitor::GetPeerConnections() const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		std::vector<PeerConnectionMonitor*> result;

		for (const auto& [id, peerConnection] : this->peerConnections)
		{
			if (!peerConnection->IsClosed())
			{
				result.push_back(peerConnection.get());
			}
		}

		return result;
	}

	std::vector<InboundTrackMonitor*> ClientMonitor::GetInboundTracks() const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		std::vector<InboundTrackMonitor*> result;

		for (auto* peerConnection : this->GetPeerConnections())
		{
			for (auto* track : peerConnection->GetInboundTracks())
			{
				result.push_back(track);
			}
		}

		return result;
	}

	std::vector<OutboundTrackMonitor*> ClientMonitor::GetOutboundTracks() const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		std::vector<OutboundTrackMonitor*> result;

		for (auto* peerConnection : this->GetPeerConnections())
		{
			for (auto* track : peerConnection->GetOutboundTracks())
			{
				result.push_back(track);
			}
		}

		return result;
	}

	InboundTrackMonitor* ClientMonitor::GetInboundTrackMonitor(const std::string& trackId) const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		for (auto* peerConnection : this->GetPeerConnections())
		{
			if (auto* track = peerConnection->GetInboundTrackMonitor(trackId))
			{
				return track;
			}
		}

		return nullptr;
	}

	OutboundTrackMonitor* ClientMonitor::GetOutboundTrackMonitor(const std::string& trackId) const
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		for (auto* peerConnection : this->GetPeerConnections())
		{
			if (auto* track = peerConnection->GetOutboundTrackMonitor(trackId))
			{
				return track;
			}
		}

		return nullptr;
	}

	void ClientMonitor::SetInboundTrackContext(const std::string& trackId, const InboundTrackContext& context)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		if (auto* track = this->GetInboundTrackMonitor(trackId))
		{
			return track->SetContext(context);
		}

		this->pendingInboundTrackContexts[trackId].MergeFrom(context);
	}

	void ClientMonitor::SetOutboundTrackContext(
	  const std::string& trackId, const OutboundTrackContext& context)
	{
		std::lock_guard<std::recursive_mutex> lock(this->mutex);

		if (auto* track = this->GetOutboundTrackMonitor(trackId))
		{
			return track->SetContext(context);
		}

		this->pendingOutboundTrackContexts[trackId].MergeFrom(context);
	}

	SlicedWindow::Config ClientMonitor::CreateWindowConfig(
	  const WindowConfig& windowConfig, size_t numberOfTotals) const
	{
		static const std::map<std::string, std::string> SitsBehind = {
			{ "recovery",     "detection"     },
			{ "flowRecovery", "flowDetection" },
		};

		SlicedWindow::Config result;
		const auto collectingPeriodInMs =
		  0 < this->config.collectingPeriodInMs ? this->config.collectingPeriodInMs : 5000;

		result.numberOfTotals = numberOfTotals;
		result.maxAllowedGapInMs =
		  static_cast<double>(windowConfig.maxAllowedGapInMs.value_or(collectingPeriodInMs * 4));

		for (const auto& [name, numberOfSamples] : windowConfig.numberOfSamples)
		{
			SlicedWindow::SliceConfig slice;

			slice.numberOfSamples = numberOfSamples;

			auto behind = SitsBehind.find(name);

			if (behind != SitsBehind.end())
			{
				auto ahead = windowConfig.numberOfSamples.find(behind->second);

				slice.offset = ahead == windowConfig.numberOfSamples.end() ? 0 : ahead->second;
			}

			result.slices.emplace(name, slice);
		}

		return result;
	}

	/* Private. */

	void ClientMonitor::BufferIssueForSample(
	  const std::string& type,
	  const json& payload,
	  int64_t timestamp,
	  const std::optional<std::string>& key)
	{
		// Only buffered when sampling is configured; reporting is unconditional.
		if (!this->IsBuffering())
		{
			return;
		}

		ClientIssue issue;

		issue.type      = type;
		issue.key       = key;
		issue.payload   = payload;
		issue.timestamp = timestamp;

		this->clientIssues.push_back(issue);
	}

	bool ClientMonitor::IsBuffering() const
	{
		return 0 < this->samplingTick || this->config.bufferingEventsForSamples;
	}

	void ClientMonitor::SetSamplingTick()
	{
		const auto collectingPeriodInMs = this->config.collectingPeriodInMs;
		const auto samplingPeriodInMs   = this->config.samplingPeriodInMs;

		if (samplingPeriodInMs < 1)
		{
			this->samplingTick = 0;

			return;
		}
		// Driven by hand (no timer): every Collect() call creates a sample.
		if (collectingPeriodInMs < 1)
		{
			this->samplingTick = 1;

			return;
		}
		if (samplingPeriodInMs % collectingPeriodInMs != 0)
		{
			CM_WARN(
			  "samplingPeriodInMs (%lld) should be a multiple of collectingPeriodInMs (%lld)",
			  static_cast<long long>(samplingPeriodInMs),
			  static_cast<long long>(collectingPeriodInMs));
		}

		this->samplingTick = std::max<int64_t>(1, samplingPeriodInMs / collectingPeriodInMs);
	}

	void ClientMonitor::FlushBufferedClientSamples()
	{
		if (!this->bufferedClientSamples.has_value() || this->listeners.empty())
		{
			return;
		}

		// Dropped before emitting, so a listener added from inside a handler finds nothing left.
		auto samples = std::move(*this->bufferedClientSamples);

		this->bufferedClientSamples.reset();

		for (const auto& sample : samples)
		{
			this->Emit(
			  [&](Listener* listener)
			  {
				  listener->OnSampleCreated(this, sample);
			  });
		}
	}

	std::optional<InboundTrackContext> ClientMonitor::TakePendingInboundTrackContext(
	  const std::string& trackId)
	{
		auto it = this->pendingInboundTrackContexts.find(trackId);

		if (it == this->pendingInboundTrackContexts.end())
		{
			return std::nullopt;
		}

		auto context = it->second;

		this->pendingInboundTrackContexts.erase(it);

		return context;
	}

	std::optional<OutboundTrackContext> ClientMonitor::TakePendingOutboundTrackContext(
	  const std::string& trackId)
	{
		auto it = this->pendingOutboundTrackContexts.find(trackId);

		if (it == this->pendingOutboundTrackContexts.end())
		{
			return std::nullopt;
		}

		auto context = it->second;

		this->pendingOutboundTrackContexts.erase(it);

		return context;
	}

	void ClientMonitor::DropClosedPeerConnections()
	{
		for (auto it = this->peerConnections.begin(); it != this->peerConnections.end();)
		{
			it = it->second->IsClosed() ? this->peerConnections.erase(it) : std::next(it);
		}
	}

	/* Methods inherited from IssueRegistrySink: the terminal of every issue chain. */

	void ClientMonitor::OnIssueRaised(const RaisedClientIssue& issue)
	{
		if (this->closed)
		{
			return;
		}

		// With lifecycle tracking on, the raise entry carries the key the `-resolved` entry closes.
		if (issue.includeInSample)
		{
			this->BufferIssueForSample(
			  issue.type,
			  issue.payload,
			  issue.raisedAt,
			  this->config.sendResolvedIssuesToServer ? std::optional<std::string>(issue.key)
			                                          : std::nullopt);
		}

		const auto reported = ReportedClientIssue::From(issue);

		this->Emit(
		  [&](Listener* listener)
		  {
			  listener->OnIssue(this, reported);
		  });
	}

	void ClientMonitor::OnIssueUpdated(const RaisedClientIssue& issue)
	{
		if (this->closed)
		{
			return;
		}

		this->Emit(
		  [&](Listener* listener)
		  {
			  listener->OnIssueUpdated(this, issue);
		  });
	}

	void ClientMonitor::OnIssueResolved(const ResolvedClientIssue& issue)
	{
		if (this->closed)
		{
			return;
		}

		if (this->config.sendResolvedIssuesToServer && issue.includeInSample)
		{
			// `key` says which open issue this closes; only the resolution's payload is flattened in.
			json payload = {
				{ "raisedAt", issue.raisedAt }
			};

			if (issue.comment.has_value())
			{
				payload["comment"] = *issue.comment;
			}
			if (issue.payload.is_object())
			{
				payload.update(issue.payload);
			}

			this->BufferIssueForSample(issue.type + "-resolved", payload, issue.resolvedAt, issue.key);
		}

		this->Emit(
		  [&](Listener* listener)
		  {
			  listener->OnIssueResolved(this, issue);
		  });
	}

	void ClientMonitor::OnIssueNotified(const AddedClientIssue& issue)
	{
		this->AddIssue(issue.type, issue.payload, issue.timestamp, issue.includeInSample.value_or(true));
	}
} // namespace observertc::clientmonitor
