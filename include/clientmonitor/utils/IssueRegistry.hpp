#ifndef CM_UTILS_ISSUE_REGISTRY_HPP
#define CM_UTILS_ISSUE_REGISTRY_HPP

#include "clientmonitor/ClientMonitorIssues.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace clientmonitor
{
	/**
	 * Where an IssueRegistry reports what happened to its issues. Every registry is itself a sink,
	 * which is how a track's registry chains up to its peer connection's and on to the client
	 * monitor's, whose own sink is the terminal one that reports and buffers into the sample.
	 */
	class IssueRegistrySink
	{
	public:
		virtual ~IssueRegistrySink()                                   = default;
		virtual void OnIssueRaised(const RaisedClientIssue& issue)     = 0;
		virtual void OnIssueUpdated(const RaisedClientIssue& issue)    = 0;
		virtual void OnIssueResolved(const ResolvedClientIssue& issue) = 0;
		virtual void OnIssueNotified(const AddedClientIssue& issue)    = 0;
	};

	/**
	 * The active issues of one subject (a client, a peer connection, a track, an ICE transport),
	 * keyed by `key`. Port of the JS `IssueRegistry`.
	 */
	class IssueRegistry : public IssueRegistrySink
	{
	public:
		struct RaiseInput
		{
			std::string key;
			std::string type;
			nlohmann::json payload;
			std::optional<int64_t> timestamp;
			std::optional<bool> includeInSample;
		};

		struct UpdateInput
		{
			std::string key;
			// Shallow-merged into the existing payload.
			nlohmann::json payload;
			std::optional<int64_t> timestamp;
			std::optional<bool> includeInSample;
		};

		struct ResolveInput
		{
			std::string key;
			std::optional<std::string> comment;
			// Shallow-merged into the existing payload.
			nlohmann::json payload;
			std::optional<int64_t> resolvedAt;
		};

		struct NotifyInput
		{
			std::string type;
			nlohmann::json payload;
			std::optional<int64_t> timestamp;
			std::optional<bool> includeInSample;
		};

	public:
		explicit IssueRegistry(IssueRegistrySink* uplink);
		~IssueRegistry() override = default;

	public:
		const RaisedClientIssue* Get(const std::string& key) const;
		std::vector<const RaisedClientIssue*> GetByType(const std::string& type) const;
		const nlohmann::json* GetFirstPayloadByType(const std::string& type) const;
		std::vector<std::string> GetKeys() const;
		size_t GetSize() const;
		bool Has(const std::string& key) const;
		bool HasType(const std::string& type) const;

		/** Returns false (and does nothing) when the key is already active. */
		bool Raise(const RaiseInput& input);
		/** Returns false when no issue with that key is active. */
		bool Update(const UpdateInput& input);
		std::optional<ResolvedClientIssue> Resolve(const ResolveInput& input);
		/** Resolves every active issue with the same comment. Returns how many it resolved. */
		size_t ResolveAll(const std::string& comment);
		void Notify(const NotifyInput& input);

		IssueRegistrySink* AsSink();

		/* Pure virtual methods inherited from IssueRegistrySink. */
	public:
		void OnIssueRaised(const RaisedClientIssue& issue) override;
		void OnIssueUpdated(const RaisedClientIssue& issue) override;
		void OnIssueResolved(const ResolvedClientIssue& issue) override;
		void OnIssueNotified(const AddedClientIssue& issue) override;

	private:
		IssueRegistrySink* uplink{ nullptr };
		// Insertion order is kept so that ResolveAll() resolves oldest first, as the JS Map does.
		std::vector<std::string> order;
		std::map<std::string, RaisedClientIssue> activeIssues;
	};
} // namespace clientmonitor

#endif
