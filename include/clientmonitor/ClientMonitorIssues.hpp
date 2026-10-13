#ifndef CM_CLIENT_MONITOR_ISSUES_HPP
#define CM_CLIENT_MONITOR_ISSUES_HPP

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace observertc::clientmonitor
{
	/**
	 * One-shot issue, produced by ClientMonitor::AddIssue(). Reported to OnIssue() and buffered
	 * into the next ClientSample, but never enters the active store and cannot be resolved.
	 */
	struct AddedClientIssue
	{
		std::string type;
		nlohmann::json payload;
		int64_t timestamp{ 0 };
		// Whether this is buffered into the next ClientSample; nullopt reads as true.
		std::optional<bool> includeInSample;
	};

	/**
	 * Stateful issue, produced by ClientMonitor::RaiseIssue(). `key` is its identity within the
	 * monitor and the handle used to resolve it; re-raising the same key updates the entry in place
	 * and reports OnIssueUpdated().
	 */
	struct RaisedClientIssue
	{
		std::string key;
		std::string type;
		nlohmann::json payload;
		// Wall-clock time the issue was first raised.
		int64_t raisedAt{ 0 };
		// Wall-clock time of the most recent raise/update call.
		int64_t updatedAt{ 0 };
		// When false, neither the raise nor the resolution entry reaches the sample.
		bool includeInSample{ true };
	};

	struct ResolvedClientIssue : public RaisedClientIssue
	{
		int64_t resolvedAt{ 0 };
		std::optional<std::string> comment;
	};

	/** Either flavour, as reported to ClientMonitor::Listener::OnIssue(). */
	struct ReportedClientIssue
	{
		// True for a RaiseIssue() issue, false for an AddIssue() one.
		bool resolvable{ false };
		// Empty for a one-shot issue.
		std::string key;
		std::string type;
		nlohmann::json payload;
		int64_t timestamp{ 0 };
		bool includeInSample{ true };

		static ReportedClientIssue From(const AddedClientIssue& issue);
		static ReportedClientIssue From(const RaisedClientIssue& issue);
	};

	inline ReportedClientIssue ReportedClientIssue::From(const AddedClientIssue& issue)
	{
		ReportedClientIssue result;

		result.resolvable      = false;
		result.type            = issue.type;
		result.payload         = issue.payload;
		result.timestamp       = issue.timestamp;
		result.includeInSample = issue.includeInSample.value_or(true);

		return result;
	}

	inline ReportedClientIssue ReportedClientIssue::From(const RaisedClientIssue& issue)
	{
		ReportedClientIssue result;

		result.resolvable      = true;
		result.key             = issue.key;
		result.type            = issue.type;
		result.payload         = issue.payload;
		result.timestamp       = issue.raisedAt;
		result.includeInSample = issue.includeInSample;

		return result;
	}
} // namespace observertc::clientmonitor

#endif
