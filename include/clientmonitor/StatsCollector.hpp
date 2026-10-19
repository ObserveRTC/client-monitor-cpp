#ifndef CM_STATS_COLLECTOR_HPP
#define CM_STATS_COLLECTOR_HPP

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <set>
#include <string>

namespace observertc::clientmonitor
{
	/**
	 * Where a PeerConnectionMonitor gets its W3C stats from on every collection.
	 *
	 * GetStats() returns the report as a JSON array of stats objects, each with at least `type`,
	 * `id` and `timestamp` (milliseconds). That is exactly what libwebrtc's
	 * `RTCStatsReport::ToJson()` and libmediasoupclient's `Transport::GetStats()` produce, so a
	 * binding is usually a few lines. It is called on the collecting thread and may block.
	 */
	class StatsCollector
	{
	public:
		virtual ~StatsCollector()         = default;
		virtual nlohmann::json GetStats() = 0;
	};

	/** A StatsCollector backed by a callable. */
	class FunctionStatsCollector : public StatsCollector
	{
	public:
		explicit FunctionStatsCollector(std::function<nlohmann::json()> getStats)
		  : getStats(std::move(getStats))
		{
		}

		nlohmann::json GetStats() override
		{
			return this->getStats ? this->getStats() : nlohmann::json::array();
		}

	private:
		std::function<nlohmann::json()> getStats;
	};

	/**
	 * Brings one peer connection's raw stats report to the W3C shape the monitors read. Port of
	 * the JS `StatsAdapter`.
	 *
	 * Adapters belong to the ClientMonitor. On every collection it takes each peer connection's raw
	 * report from its StatsCollector, runs every adapter on it in the order they were added, and
	 * only then hands it to the peer connection monitor. Adapt() rewrites the report in place.
	 */
	class StatsAdapter
	{
	public:
		virtual ~StatsAdapter()                    = default;
		virtual const std::string& GetName() const = 0;
		virtual void Adapt(const std::string& peerConnectionId, nlohmann::json& stats) = 0;
	};

	/**
	 * Brings a libwebrtc (Chromium) report to the shape the monitors read: folds legacy `track`
	 * stats into `inbound-rtp`, `mediaType` into `kind`, `ip` into `address`, and infers the
	 * cross references (`mediaSourceId`, `transportId`, `remoteId`/`localId`, `codecId`) that a
	 * report leaves out when there is only one candidate. Port of the JS `ChromeStatsAdapter`.
	 *
	 * It also brings `timestamp` to milliseconds (a DOMHighResTimeStamp): libwebrtc's
	 * `RTCStatsReport::ToJson()` writes microseconds.
	 */
	class LibWebRtcStatsAdapter : public StatsAdapter
	{
	public:
		const std::string& GetName() const override;
		void Adapt(const std::string& peerConnectionId, nlohmann::json& stats) override;
	};

	/**
	 * Unlinks the `outbound-rtp` entries of senders that carry no track from their media source.
	 *
	 * In W3C stats, `outbound-rtp.mediaSourceId` names the `media-source` of the track currently
	 * attached to the stream's sender, so a sender without a track has none. libwebrtc keeps
	 * reporting such a sender (one whose track was removed when its producer closed) with a
	 * `mediaSourceId`: the source of the track it used to carry, or even the source of the track a
	 * newer sender carries. Through that reference a track is bound to streams that no longer send
	 * it.
	 *
	 * The report does not say which senders carry a track; the application or binding that owns
	 * the senders does, per peer connection and by `mid`: SetSenderTrack(peerConnectionId, mid, true)
	 * when a track is attached to the sender, SetSenderTrack(peerConnectionId, mid, false) when it is
	 * removed or the sender is closed. For a peer connection it was told about, Adapt() removes
	 * `mediaSourceId` from every `outbound-rtp` whose `mid` is not a sender with a track. Other peer
	 * connections' reports pass unchanged, and nothing else changes: the entries and media sources
	 * stay in the report.
	 *
	 * Add it to the ClientMonitor after the adapters that infer `mediaSourceId`. The ClientMonitor
	 * starts with a LibWebRtcStatsAdapter, so any adapter added later runs after it.
	 */
	class DetachedSenderStatsAdapter : public StatsAdapter
	{
	public:
		static const std::string Name;

		const std::string& GetName() const override;
		void Adapt(const std::string& peerConnectionId, nlohmann::json& stats) override;

		/** Whether the sender with `mid` on `peerConnectionId` carries a track now. */
		void SetSenderTrack(const std::string& peerConnectionId, const std::string& mid, bool hasTrack);
		/** Forgets a peer connection; its reports pass unchanged from then on. */
		void RemovePeerConnection(const std::string& peerConnectionId);

	private:
		// Peer connection id -> mids of its senders that carry a track.
		std::map<std::string, std::set<std::string>> sendersWithTrack;
	};
} // namespace observertc::clientmonitor

#endif
