#ifndef CM_MONITORS_STATS_MONITOR_HPP
#define CM_MONITORS_STATS_MONITOR_HPP

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace observertc::clientmonitor
{
	// Fast forward declarations.
	class PeerConnectionMonitor;

	/**
	 * What every monitor of one W3C stats object has in common: the latest report it accepted,
	 * the peer connection it belongs to, and the bookkeeping the peer connection uses to drop it
	 * once the report stops arriving.
	 *
	 * The raw report is GetStats(); everything a monitor derives from consecutive reports (deltas,
	 * rates, ratios) is on its own GetMetrics().
	 */
	template<typename T>
	class StatsMonitor
	{
	public:
		virtual ~StatsMonitor() = default;

	public:
		/** The latest report accepted, as the schema carries it. */
		const T& GetStats() const
		{
			return this->stats;
		}
		const std::string& GetId() const
		{
			return this->stats.id;
		}
		double GetTimestamp() const
		{
			return this->stats.timestamp;
		}
		PeerConnectionMonitor* GetPeerConnection() const
		{
			return this->peerConnection;
		}
		/** Milliseconds between the last two reports, or nullopt before the second one. */
		std::optional<double> GetDeltaTime() const
		{
			return this->deltaTime;
		}
		/** Sum of every deltaTime this monitor has seen: its own stats clock. */
		double GetStatsClockTime() const
		{
			return this->statsClockTime;
		}
		/** Application data, never read by the library. */
		nlohmann::json& GetAppData()
		{
			return this->appData;
		}
		/**
		 * Shipped as `attachments` on this object's entry in every sample, so the observer can
		 * join it to server-side ids (a mediasoup producer, consumer, data producer, ...). Kept
		 * across reports: the raw stats never carry attachments, so they are stamped on at
		 * sampling time. Null (the default) sends none.
		 */
		nlohmann::json& GetAttachments()
		{
			return this->attachments;
		}
		/** Merges `patch` into the attachments (RFC 7386: a null value removes that key). */
		void UpdateAttachments(const nlohmann::json& patch)
		{
			this->attachments.merge_patch(patch);
		}
		/** The latest report, as it goes into a sample. */
		virtual T CreateSample()
		{
			T sample = this->stats;

			if (!this->attachments.is_null())
			{
				sample.attachments = this->attachments;
			}

			return sample;
		}

		/**
		 * Whether a report reached this monitor since the last call. Reading clears it, as the JS
		 * `visited` getter does, so the peer connection can tell which monitors went quiet.
		 */
		bool ConsumeVisited()
		{
			const bool result = this->visited;

			this->visited = false;

			return result;
		}

	protected:
		StatsMonitor(PeerConnectionMonitor* peerConnection, const T& stats)
		  : peerConnection(peerConnection), stats(stats)
		{
		}

		/**
		 * Marks the monitor visited and returns the milliseconds since the previous report, or
		 * nullopt when the clock did not move (in which case nothing can be derived).
		 */
		std::optional<double> Visit(const T& next)
		{
			this->visited = true;

			const double elapsedInMs = next.timestamp - this->stats.timestamp;

			if (elapsedInMs <= 0)
			{
				return std::nullopt;
			}

			this->deltaTime = elapsedInMs;
			this->statsClockTime += elapsedInMs;

			return elapsedInMs;
		}

	protected:
		PeerConnectionMonitor* peerConnection{ nullptr };
		T stats;
		bool visited{ true };
		std::optional<double> deltaTime;
		double statsClockTime{ 0 };
		nlohmann::json appData = nlohmann::json::object();
		nlohmann::json attachments;
	};
} // namespace observertc::clientmonitor

#endif
