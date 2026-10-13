#define CM_CLASS "IssueRegistry"

#include "clientmonitor/utils/IssueRegistry.hpp"
#include "clientmonitor/utils/Utils.hpp"
#include "clientmonitor/Logger.hpp"

#include <algorithm>

namespace observertc::clientmonitor
{
	namespace
	{
		void MergePayload(nlohmann::json& target, const nlohmann::json& patch)
		{
			if (patch.is_null())
			{
				return;
			}
			if (!target.is_object() || !patch.is_object())
			{
				target = patch;

				return;
			}

			target.update(patch);
		}
	} // namespace

	IssueRegistry::IssueRegistry(IssueRegistrySink* uplink) : uplink(uplink)
	{
		CM_TRACE();
	}

	const RaisedClientIssue* IssueRegistry::Get(const std::string& key) const
	{
		auto it = this->activeIssues.find(key);

		return it == this->activeIssues.end() ? nullptr : &it->second;
	}

	std::vector<const RaisedClientIssue*> IssueRegistry::GetByType(const std::string& type) const
	{
		std::vector<const RaisedClientIssue*> result;

		for (const auto& key : this->order)
		{
			const auto& issue = this->activeIssues.at(key);

			if (issue.type == type)
			{
				result.push_back(&issue);
			}
		}

		return result;
	}

	const nlohmann::json* IssueRegistry::GetFirstPayloadByType(const std::string& type) const
	{
		for (const auto& key : this->order)
		{
			const auto& issue = this->activeIssues.at(key);

			if (issue.type == type)
			{
				return &issue.payload;
			}
		}

		return nullptr;
	}

	std::vector<std::string> IssueRegistry::GetKeys() const
	{
		return this->order;
	}

	size_t IssueRegistry::GetSize() const
	{
		return this->activeIssues.size();
	}

	bool IssueRegistry::Has(const std::string& key) const
	{
		return this->activeIssues.find(key) != this->activeIssues.end();
	}

	bool IssueRegistry::HasType(const std::string& type) const
	{
		return this->GetFirstPayloadByType(type) != nullptr;
	}

	bool IssueRegistry::Raise(const RaiseInput& input)
	{
		if (this->Has(input.key))
		{
			return false;
		}

		const auto now = input.timestamp.value_or(utils::NowMs());
		RaisedClientIssue issue;

		issue.key             = input.key;
		issue.type            = input.type;
		issue.payload         = input.payload;
		issue.raisedAt        = now;
		issue.updatedAt       = now;
		issue.includeInSample = input.includeInSample.value_or(true);

		this->activeIssues.emplace(issue.key, issue);
		this->order.push_back(issue.key);

		if (this->uplink)
		{
			this->uplink->OnIssueRaised(issue);
		}

		return true;
	}

	bool IssueRegistry::Update(const UpdateInput& input)
	{
		auto it = this->activeIssues.find(input.key);

		if (it == this->activeIssues.end())
		{
			return false;
		}

		auto& existing = it->second;

		MergePayload(existing.payload, input.payload);

		if (input.includeInSample.has_value())
		{
			existing.includeInSample = *input.includeInSample;
		}

		existing.updatedAt = input.timestamp.value_or(utils::NowMs());

		if (this->uplink)
		{
			this->uplink->OnIssueUpdated(existing);
		}

		return true;
	}

	std::optional<ResolvedClientIssue> IssueRegistry::Resolve(const ResolveInput& input)
	{
		auto it = this->activeIssues.find(input.key);

		if (it == this->activeIssues.end())
		{
			return std::nullopt;
		}

		ResolvedClientIssue resolved;

		static_cast<RaisedClientIssue&>(resolved) = it->second;

		if (!resolved.payload.is_object())
		{
			resolved.payload = nlohmann::json::object();
		}

		MergePayload(resolved.payload, input.payload);

		resolved.resolvedAt = input.resolvedAt.value_or(utils::NowMs());
		resolved.comment    = input.comment;

		this->activeIssues.erase(it);
		this->order.erase(
		  std::remove(this->order.begin(), this->order.end(), input.key), this->order.end());

		if (this->uplink)
		{
			this->uplink->OnIssueResolved(resolved);
		}

		return resolved;
	}

	size_t IssueRegistry::ResolveAll(const std::string& comment)
	{
		const auto keys = this->order;

		for (const auto& key : keys)
		{
			this->Resolve({ key, comment, nlohmann::json(), std::nullopt });
		}

		return keys.size();
	}

	void IssueRegistry::Notify(const NotifyInput& input)
	{
		AddedClientIssue issue;

		issue.type            = input.type;
		issue.payload         = input.payload;
		issue.timestamp       = input.timestamp.value_or(utils::NowMs());
		issue.includeInSample = input.includeInSample;

		if (this->uplink)
		{
			this->uplink->OnIssueNotified(issue);
		}
	}

	IssueRegistrySink* IssueRegistry::AsSink()
	{
		return this;
	}

	/* Methods inherited from IssueRegistrySink: a child registry reporting into this one. */

	void IssueRegistry::OnIssueRaised(const RaisedClientIssue& issue)
	{
		this->Raise({ issue.key, issue.type, issue.payload, issue.raisedAt, issue.includeInSample });
	}

	void IssueRegistry::OnIssueUpdated(const RaisedClientIssue& issue)
	{
		this->Update({ issue.key, issue.payload, issue.updatedAt, issue.includeInSample });
	}

	void IssueRegistry::OnIssueResolved(const ResolvedClientIssue& issue)
	{
		this->Resolve({ issue.key, issue.comment, issue.payload, issue.resolvedAt });
	}

	void IssueRegistry::OnIssueNotified(const AddedClientIssue& issue)
	{
		this->Notify({ issue.type, issue.payload, issue.timestamp, issue.includeInSample });
	}
} // namespace observertc::clientmonitor
