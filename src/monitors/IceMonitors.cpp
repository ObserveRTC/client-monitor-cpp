#define CM_CLASS "IceMonitors"

#include "clientmonitor/monitors/IceMonitors.hpp"
#include "clientmonitor/utils/Utils.hpp"
#include "clientmonitor/ClientMonitor.hpp"
#include "clientmonitor/Logger.hpp"
#include "clientmonitor/monitors/PeerConnectionMonitor.hpp"
#include "clientmonitor/monitors/RtpMonitors.hpp"

#include <regex>

using observertc::clientmonitor::utils::PositiveDelta;

namespace observertc::clientmonitor
{
	/* IceTransportMonitor */

	IceTransportMonitor::IceTransportMonitor(
	  PeerConnectionMonitor* peerConnection, const IceTransportStats& stats)
	  : StatsMonitor<IceTransportStats>(peerConnection, stats),
	    issues(peerConnection->GetIssues().AsSink())
	{
		// BlockedStunRequestsDetector lives here once ported: it judges one transport.
	}

	void IceTransportMonitor::Accept(const IceTransportStats& next)
	{
		const auto elapsed = this->Visit(next);

		if (!elapsed.has_value())
		{
			return;
		}

		const auto& prev          = this->stats;
		auto& m                   = this->metrics;
		const double elapsedInSec = *elapsed / 1000;

		if (next.iceState == "connected" || next.iceState == "completed")
		{
			this->everConnected = true;
		}

		m.deltaPacketsSent     = PositiveDelta(next.packetsSent, prev.packetsSent);
		m.deltaPacketsReceived = PositiveDelta(next.packetsReceived, prev.packetsReceived);
		m.deltaBytesSent       = PositiveDelta(next.bytesSent, prev.bytesSent);
		m.deltaBytesReceived   = PositiveDelta(next.bytesReceived, prev.bytesReceived);
		m.sendingBitrate       = m.deltaBytesSent.has_value()
		                           ? std::optional<double>((*m.deltaBytesSent * 8) / elapsedInSec)
		                           : std::nullopt;
		m.receivingBitrate     = m.deltaBytesReceived.has_value()
		                           ? std::optional<double>((*m.deltaBytesReceived * 8) / elapsedInSec)
		                           : std::nullopt;
		m.deltaSelectedCandidatePairChanges =
		  prev.selectedCandidatePairId.has_value()
		    ? PositiveDelta(next.selectedCandidatePairChanges, prev.selectedCandidatePairChanges)
		    : std::nullopt;

		this->stats = next;
	}

	IceTransportStats IceTransportMonitor::CreateSample()
	{
		auto sample = StatsMonitor::CreateSample();

		if (!this->peerConnection->GetParent()->GetConfig().sendIceTransportMetadataOnChangeOnly)
		{
			return sample;
		}

		// Constant after the handshake, so emitted in the first sample and again only on change
		// (the ufrag changes exactly at an ICE restart).
		const auto& s    = this->stats;
		const auto field = [](const std::optional<std::string>& value)
		{
			return value.value_or("");
		};
		const auto fingerprint = field(s.iceRole) + "|" + field(s.iceLocalUsernameFragment) + "|" +
		                         field(s.localCertificateId) + "|" + field(s.remoteCertificateId) +
		                         "|" + field(s.tlsVersion) + "|" + field(s.dtlsCipher) + "|" +
		                         field(s.dtlsRole) + "|" + field(s.srtpCipher);

		if (this->sampledStaticMetadataFingerprint == fingerprint)
		{
			sample.iceRole.reset();
			sample.iceLocalUsernameFragment.reset();
			sample.localCertificateId.reset();
			sample.remoteCertificateId.reset();
			sample.tlsVersion.reset();
			sample.dtlsCipher.reset();
			sample.dtlsRole.reset();
			sample.srtpCipher.reset();
		}
		else
		{
			this->sampledStaticMetadataFingerprint = fingerprint;
		}

		return sample;
	}

	IceCandidatePairMonitor* IceTransportMonitor::GetSelectedCandidatePair() const
	{
		return this->peerConnection->GetIceCandidatePairMonitor(
		  this->stats.selectedCandidatePairId.value_or(""));
	}

	std::vector<InboundRtpMonitor*> IceTransportMonitor::GetInboundRtps() const
	{
		std::vector<InboundRtpMonitor*> result;

		for (auto* rtp : this->peerConnection->GetInboundRtps())
		{
			if (rtp->GetStats().transportId == this->stats.id)
			{
				result.push_back(rtp);
			}
		}

		return result;
	}

	std::vector<OutboundRtpMonitor*> IceTransportMonitor::GetOutboundRtps() const
	{
		std::vector<OutboundRtpMonitor*> result;

		for (auto* rtp : this->peerConnection->GetOutboundRtps())
		{
			if (rtp->GetStats().transportId == this->stats.id)
			{
				result.push_back(rtp);
			}
		}

		return result;
	}

	/* IceCandidateMonitor */

	IceCandidateMonitor::IceCandidateMonitor(
	  PeerConnectionMonitor* peerConnection, const IceCandidateStats& stats, Direction direction)
	  : StatsMonitor<IceCandidateStats>(peerConnection, stats), direction(direction)
	{
	}

	void IceCandidateMonitor::Accept(const IceCandidateStats& next)
	{
		if (!this->Visit(next).has_value())
		{
			return;
		}

		this->stats = next;
	}

	bool IceCandidateMonitor::IsRelay() const
	{
		return this->stats.candidateType == "relay";
	}

	std::optional<std::string> IceCandidateMonitor::GetTurnTransport() const
	{
		const auto& protocol = this->stats.relayProtocol;

		if (protocol == "udp" || protocol == "tcp" || protocol == "tls")
		{
			return protocol;
		}

		return std::nullopt;
	}

	std::optional<std::string> IceCandidateMonitor::GetTurnServer() const
	{
		if (!this->IsRelay() || !this->stats.url.has_value() || this->stats.url->rfind("turn", 0) != 0)
		{
			return std::nullopt;
		}

		return this->stats.url->substr(0, this->stats.url->find('?'));
	}

	std::optional<std::string> IceCandidateMonitor::GetAddressFamily() const
	{
		static const std::regex Ipv4(R"(^\d{1,3}(\.\d{1,3}){3}$)");

		const auto& address = this->stats.address;

		if (!address.has_value() || address->empty())
		{
			return std::nullopt;
		}
		if (address->size() >= 6 && address->compare(address->size() - 6, 6, ".local") == 0)
		{
			return std::nullopt;
		}
		if (address->find(':') != std::string::npos)
		{
			return "ipv6";
		}
		if (std::regex_match(*address, Ipv4))
		{
			return "ipv4";
		}

		return std::nullopt;
	}

	IceTransportMonitor* IceCandidateMonitor::GetIceTransport() const
	{
		return this->peerConnection->GetIceTransportMonitor(this->stats.transportId.value_or(""));
	}

	/* IceCandidatePairMonitor */

	IceCandidatePairMonitor::IceCandidatePairMonitor(
	  PeerConnectionMonitor* peerConnection, const IceCandidatePairStats& stats)
	  : StatsMonitor<IceCandidatePairStats>(peerConnection, stats)
	{
	}

	void IceCandidatePairMonitor::Accept(const IceCandidatePairStats& next)
	{
		if (!this->Visit(next).has_value())
		{
			return;
		}

		const auto& prev = this->stats;
		auto& m          = this->metrics;

		m.deltaPacketsSent         = PositiveDelta(next.packetsSent, prev.packetsSent);
		m.deltaPacketsReceived     = PositiveDelta(next.packetsReceived, prev.packetsReceived);
		m.deltaBytesSent           = PositiveDelta(next.bytesSent, prev.bytesSent);
		m.deltaBytesReceived       = PositiveDelta(next.bytesReceived, prev.bytesReceived);
		m.deltaTotalRoundTripTime  = PositiveDelta(next.totalRoundTripTime, prev.totalRoundTripTime);
		m.deltaResponsesReceived   = PositiveDelta(next.responsesReceived, prev.responsesReceived);
		m.deltaRequestsSent        = PositiveDelta(next.requestsSent, prev.requestsSent);
		m.deltaConsentRequestsSent = PositiveDelta(next.consentRequestsSent, prev.consentRequestsSent);
		m.deltaPacketsDiscardedOnSend =
		  PositiveDelta(next.packetsDiscardedOnSend, prev.packetsDiscardedOnSend);
		m.deltaBytesDiscardedOnSend = PositiveDelta(next.bytesDiscardedOnSend, prev.bytesDiscardedOnSend);
		m.avgRoundTripTimeInSec =
		  m.deltaTotalRoundTripTime.has_value() && m.deltaResponsesReceived.has_value() &&
		      *m.deltaResponsesReceived > 0
		    ? std::optional<double>(*m.deltaTotalRoundTripTime / *m.deltaResponsesReceived)
		    : std::nullopt;

		this->stats = next;
	}

	IceTransportMonitor* IceCandidatePairMonitor::GetIceTransport() const
	{
		return this->peerConnection->GetIceTransportMonitor(this->stats.transportId.value_or(""));
	}

	IceCandidateMonitor* IceCandidatePairMonitor::GetLocalCandidate() const
	{
		return this->peerConnection->GetIceCandidateMonitor(this->stats.localCandidateId.value_or(""));
	}

	IceCandidateMonitor* IceCandidatePairMonitor::GetRemoteCandidate() const
	{
		return this->peerConnection->GetIceCandidateMonitor(this->stats.remoteCandidateId.value_or(""));
	}

	std::string IceCandidatePairMonitor::GetPathKey() const
	{
		if (this->stats.transportId.has_value())
		{
			return *this->stats.transportId;
		}

		auto* local = this->GetLocalCandidate();

		if (local && local->GetStats().transportId.has_value())
		{
			return *local->GetStats().transportId;
		}

		return "unknown-ice-transport";
	}

	bool IceCandidatePairMonitor::IsUsingTurn() const
	{
		auto* local = this->GetLocalCandidate();

		return local && local->IsRelay();
	}

	bool IceCandidatePairMonitor::IsUsingTcp() const
	{
		auto* local = this->GetLocalCandidate();

		return local && local->GetStats().protocol == "tcp";
	}

	std::optional<std::string> IceCandidatePairMonitor::GetRelayProtocol() const
	{
		auto* local = this->GetLocalCandidate();

		return local ? local->GetTurnTransport() : std::nullopt;
	}

	std::optional<std::string> IceCandidatePairMonitor::GetTurnUrl() const
	{
		auto* local = this->GetLocalCandidate();

		return this->IsUsingTurn() && local ? local->GetStats().url : std::nullopt;
	}

	std::optional<std::string> IceCandidatePairMonitor::GetTurnServer() const
	{
		auto* local = this->GetLocalCandidate();

		return local ? local->GetTurnServer() : std::nullopt;
	}

	std::string IceCandidatePairMonitor::GetPathKind() const
	{
		if (!this->IsUsingTurn())
		{
			return "direct";
		}

		const auto protocol = this->GetRelayProtocol();

		if (protocol == "udp")
		{
			return "turn-udp";
		}
		if (protocol == "tcp")
		{
			return "turn-tcp";
		}
		if (protocol == "tls")
		{
			return "turn-tls";
		}

		return "turn-unknown";
	}

	std::string IceCandidatePairMonitor::GetTuple() const
	{
		auto* local  = this->GetLocalCandidate();
		auto* remote = this->GetRemoteCandidate();

		const auto address = [](IceCandidateMonitor* candidate)
		{
			return candidate && candidate->GetStats().address ? *candidate->GetStats().address
			                                                  : "undefined";
		};
		const auto port = [](IceCandidateMonitor* candidate)
		{
			return candidate && candidate->GetStats().port ? std::to_string(*candidate->GetStats().port)
			                                               : "undefined";
		};
		const auto protocol =
		  local && local->GetStats().protocol ? *local->GetStats().protocol : "undefined";

		return address(local) + ":" + port(local) + ":" + address(remote) + ":" + port(remote) + ":" +
		       protocol;
	}
} // namespace observertc::clientmonitor
