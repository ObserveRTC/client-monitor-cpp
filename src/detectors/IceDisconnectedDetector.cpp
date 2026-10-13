#define CM_CLASS "IceDisconnectedDetector"

#include "clientmonitor/detectors/IceDisconnectedDetector.hpp"
#include "clientmonitor/utils/Utils.hpp"
#include "clientmonitor/ClientMonitor.hpp"
#include "clientmonitor/Logger.hpp"
#include "clientmonitor/monitors/PeerConnectionMonitor.hpp"

#include <set>

namespace observertc::clientmonitor
{
	IceDisconnectedDetector::IceDisconnectedDetector(PeerConnectionMonitor* peerConnection)
	  : peerConnection(peerConnection)
	{
	}

	const std::string& IceDisconnectedDetector::GetName() const
	{
		static const std::string Name{ "ice-disconnected-detector" };

		return Name;
	}

	void IceDisconnectedDetector::Update()
	{
		if (this->disabled || this->peerConnection->IsClosed())
		{
			return;
		}
		if (!this->peerConnection->GetParent()->GetConfig().iceDisconnectedDetector.has_value())
		{
			return;
		}

		std::set<std::string> seenIds;

		for (auto* transport : this->peerConnection->GetIceTransports())
		{
			seenIds.insert(transport->GetId());
			this->CheckTransport(transport);
		}

		for (auto it = this->states.begin(); it != this->states.end();)
		{
			if (seenIds.count(it->first))
			{
				++it;

				continue;
			}

			this->Resolve(it->first, "ice transport is gone");
			it = this->states.erase(it);
		}
	}

	void IceDisconnectedDetector::CheckTransport(IceTransportMonitor* transport)
	{
		const auto& config = *this->peerConnection->GetParent()->GetConfig().iceDisconnectedDetector;
		auto& state        = this->GetState(transport);
		const auto ufrag   = UsernameFragmentOf(transport);
		const auto& transportId = transport->GetId();

		// A new local ufrag is an ICE restart: a fresh attempt, judged on its own.
		if (ufrag.has_value() && state.usernameFragment.has_value() && *ufrag != *state.usernameFragment)
		{
			state.iceGeneration += 1;
			state.disconnectedForInMs = 0;
			this->Resolve(transportId, "ice restarted");
		}
		if (ufrag.has_value())
		{
			state.usernameFragment = ufrag;
		}

		const auto& iceState = transport->GetStats().iceState;

		if (iceState == "connected" || iceState == "completed")
		{
			state.disconnectedForInMs = 0;
			this->Resolve(transportId, "ice connection recovered");

			return;
		}
		if (iceState != "disconnected")
		{
			state.disconnectedForInMs = 0;

			return;
		}

		state.disconnectedForInMs += transport->GetDeltaTime().value_or(0);

		if (state.disconnectedForInMs < static_cast<double>(config.disconnectedThresholdInMs))
		{
			return;
		}
		if (state.raisedAt.has_value())
		{
			return;
		}

		state.raisedAt = utils::NowMs();

		nlohmann::json payload = {
			{ "peerConnectionId",  this->peerConnection->GetPeerConnectionId() },
			{ "transportId",       transportId                                 },
			{ "iceState",          *iceState			                             },
			{ "disconnectedForMs", state.disconnectedForInMs                   },
			{ "iceGeneration",     state.iceGeneration                         },
		};

		if (transport->GetStats().dtlsState.has_value())
		{
			payload["dtlsState"] = *transport->GetStats().dtlsState;
		}
		if (transport->GetStats().selectedCandidatePairId.has_value())
		{
			payload["selectedCandidatePairId"] = *transport->GetStats().selectedCandidatePairId;
		}

		IssueRegistry::RaiseInput input;

		input.key             = this->IssueKey(transportId);
		input.type            = IssueType;
		input.payload         = payload;
		input.includeInSample = this->includeIssueInSample;

		this->peerConnection->GetIssues().Raise(input);
	}

	std::optional<std::string> IceDisconnectedDetector::UsernameFragmentOf(IceTransportMonitor* transport)
	{
		if (transport->GetStats().iceLocalUsernameFragment.has_value())
		{
			return transport->GetStats().iceLocalUsernameFragment;
		}

		auto* pair  = transport->GetSelectedCandidatePair();
		auto* local = pair ? pair->GetLocalCandidate() : nullptr;

		return local ? local->GetStats().usernameFragment : std::nullopt;
	}

	IceDisconnectedDetector::TransportState& IceDisconnectedDetector::GetState(IceTransportMonitor* transport)
	{
		auto it = this->states.find(transport->GetId());

		if (it != this->states.end())
		{
			return it->second;
		}

		TransportState state;

		state.usernameFragment = UsernameFragmentOf(transport);

		return this->states.emplace(transport->GetId(), state).first->second;
	}

	void IceDisconnectedDetector::Resolve(const std::string& transportId, const std::string& comment)
	{
		auto it = this->states.find(transportId);

		if (it == this->states.end() || !it->second.raisedAt.has_value())
		{
			return;
		}

		const auto raisedAt = *it->second.raisedAt;

		it->second.raisedAt.reset();

		auto& issues      = this->peerConnection->GetIssues();
		const auto key    = this->IssueKey(transportId);
		const auto* issue = issues.Get(key);

		if (!issue)
		{
			return;
		}

		auto payload            = issue->payload;
		payload["durationInMs"] = utils::NowMs() - raisedAt;

		issues.Resolve({ key, comment, payload, utils::NowMs() });
	}

	std::string IceDisconnectedDetector::IssueKey(const std::string& transportId) const
	{
		return std::string(IssueType) + "-pc-" + this->peerConnection->GetPeerConnectionId() +
		       "-transport-" + transportId;
	}
} // namespace observertc::clientmonitor
