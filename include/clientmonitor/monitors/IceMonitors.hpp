#ifndef CM_MONITORS_ICE_MONITORS_HPP
#define CM_MONITORS_ICE_MONITORS_HPP

#include "clientmonitor/utils/IssueRegistry.hpp"
#include "clientmonitor/detectors/Detector.hpp"
#include "clientmonitor/monitors/StatsMonitor.hpp"
#include "clientmonitor/schema/SchemaTypes.hpp"

#include <optional>
#include <string>
#include <vector>

namespace observertc::clientmonitor
{
	// Fast forward declarations.
	class IceCandidateMonitor;
	class IceCandidatePairMonitor;
	class InboundRtpMonitor;
	class OutboundRtpMonitor;

	/**
	 * One ICE/DTLS transport (`transport`). Owns the detectors that judge one transport rather
	 * than the whole connection.
	 */
	class IceTransportMonitor : public StatsMonitor<IceTransportStats>
	{
	public:
		struct Metrics
		{
			std::optional<double> deltaPacketsSent;
			std::optional<double> deltaPacketsReceived;
			std::optional<double> deltaBytesSent;
			std::optional<double> deltaBytesReceived;
			std::optional<double> sendingBitrate;
			std::optional<double> receivingBitrate;
			std::optional<double> deltaSelectedCandidatePairChanges;
		};

	public:
		IceTransportMonitor(PeerConnectionMonitor* peerConnection, const IceTransportStats& stats);

	public:
		void Accept(const IceTransportStats& stats);
		/** Leaves out the static metadata when it did not change, if configured to. */
		IceTransportStats CreateSample() override;
		const Metrics& GetMetrics() const
		{
			return this->metrics;
		}
		/** Whether this transport ever reached `connected` or `completed`. */
		bool IsEverConnected() const
		{
			return this->everConnected;
		}
		/** True while STUN goes out and nothing comes back (BlockedStunRequestsDetector). */
		bool IsBlocked() const
		{
			return this->blocked;
		}
		void SetBlocked(bool blocked)
		{
			this->blocked = blocked;
		}
		Detectors& GetDetectors()
		{
			return this->detectors;
		}
		IssueRegistry& GetIssues()
		{
			return this->issues;
		}

		IceCandidatePairMonitor* GetSelectedCandidatePair() const;
		std::vector<InboundRtpMonitor*> GetInboundRtps() const;
		std::vector<OutboundRtpMonitor*> GetOutboundRtps() const;

	private:
		Metrics metrics;
		bool everConnected{ false };
		bool blocked{ false };
		std::optional<std::string> sampledStaticMetadataFingerprint;
		IssueRegistry issues;
		Detectors detectors;
	};

	/** One ICE candidate, local or remote. */
	class IceCandidateMonitor : public StatsMonitor<IceCandidateStats>
	{
	public:
		enum class Direction
		{
			LOCAL,
			REMOTE
		};

	public:
		IceCandidateMonitor(
		  PeerConnectionMonitor* peerConnection, const IceCandidateStats& stats, Direction direction);

	public:
		void Accept(const IceCandidateStats& stats);
		Direction GetDirection() const
		{
			return this->direction;
		}
		bool IsRelay() const;
		/** "udp", "tcp" or "tls" for a relay candidate's client-to-TURN leg. */
		std::optional<std::string> GetTurnTransport() const;
		/** The TURN server URL without its query, for a relay candidate. */
		std::optional<std::string> GetTurnServer() const;
		/** "ipv4" or "ipv6"; nullopt for an mDNS (.local) or unparseable address. */
		std::optional<std::string> GetAddressFamily() const;
		IceTransportMonitor* GetIceTransport() const;

	private:
		Direction direction;
	};

	/** One ICE candidate pair (`candidate-pair`). */
	class IceCandidatePairMonitor : public StatsMonitor<IceCandidatePairStats>
	{
	public:
		struct Metrics
		{
			std::optional<double> deltaPacketsSent;
			std::optional<double> deltaPacketsReceived;
			std::optional<double> deltaBytesSent;
			std::optional<double> deltaBytesReceived;
			std::optional<double> deltaTotalRoundTripTime;
			std::optional<double> deltaRequestsSent;
			std::optional<double> deltaConsentRequestsSent;
			std::optional<double> deltaResponsesReceived;
			std::optional<double> deltaPacketsDiscardedOnSend;
			std::optional<double> deltaBytesDiscardedOnSend;
			// Interval mean round trip over the checks that completed since the last report.
			std::optional<double> avgRoundTripTimeInSec;
		};

	public:
		IceCandidatePairMonitor(PeerConnectionMonitor* peerConnection, const IceCandidatePairStats& stats);

	public:
		void Accept(const IceCandidatePairStats& stats);
		const Metrics& GetMetrics() const
		{
			return this->metrics;
		}
		IceTransportMonitor* GetIceTransport() const;
		IceCandidateMonitor* GetLocalCandidate() const;
		IceCandidateMonitor* GetRemoteCandidate() const;
		/** The transport this pair belongs to, which keys its selected ICE path. */
		std::string GetPathKey() const;
		bool IsUsingTurn() const;
		bool IsUsingTcp() const;
		std::optional<std::string> GetRelayProtocol() const;
		std::optional<std::string> GetTurnUrl() const;
		std::optional<std::string> GetTurnServer() const;
		/** "direct", "turn-udp", "turn-tcp", "turn-tls" or "turn-unknown". */
		std::string GetPathKind() const;
		/** "localAddress:localPort:remoteAddress:remotePort:protocol". */
		std::string GetTuple() const;

	private:
		Metrics metrics;
	};
} // namespace observertc::clientmonitor

#endif
