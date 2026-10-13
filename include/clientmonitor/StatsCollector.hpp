#ifndef CM_STATS_COLLECTOR_HPP
#define CM_STATS_COLLECTOR_HPP

#include <nlohmann/json.hpp>

#include <functional>
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
	 * Rewrites a raw stats report before the monitors read it. Port of the JS `StatsAdapter`:
	 * Adapt() runs once per collection on the whole report, in place.
	 */
	class StatsAdapter
	{
	public:
		virtual ~StatsAdapter()                    = default;
		virtual const std::string& GetName() const = 0;
		virtual void Adapt(nlohmann::json& stats)  = 0;
	};

	/**
	 * Brings a libwebrtc (Chromium) report to the shape the monitors read: folds legacy `track`
	 * stats into `inbound-rtp`, `mediaType` into `kind`, `ip` into `address`, and infers the
	 * cross references (`mediaSourceId`, `transportId`, `remoteId`/`localId`, `codecId`) that a
	 * report leaves out when there is only one candidate. Port of the JS `ChromeStatsAdapter`.
	 */
	class LibWebRtcStatsAdapter : public StatsAdapter
	{
	public:
		const std::string& GetName() const override;
		void Adapt(nlohmann::json& stats) override;
	};
} // namespace observertc::clientmonitor

#endif
