#include "clientmonitor/utils/IssueRegistry.hpp"

#include <catch.hpp>

using namespace clientmonitor;

namespace
{
	class RecordingSink : public IssueRegistrySink
	{
	public:
		void OnIssueRaised(const RaisedClientIssue& issue) override
		{
			raised.push_back(issue);
		}
		void OnIssueUpdated(const RaisedClientIssue& issue) override
		{
			updated.push_back(issue);
		}
		void OnIssueResolved(const ResolvedClientIssue& issue) override
		{
			resolved.push_back(issue);
		}
		void OnIssueNotified(const AddedClientIssue& issue) override
		{
			notified.push_back(issue);
		}

		std::vector<RaisedClientIssue> raised;
		std::vector<RaisedClientIssue> updated;
		std::vector<ResolvedClientIssue> resolved;
		std::vector<AddedClientIssue> notified;
	};
} // namespace

TEST_CASE("IssueRegistry", "[utils]")
{
	RecordingSink sink;
	IssueRegistry parent(&sink);
	IssueRegistry child(parent.AsSink());

	SECTION("a raise travels up the chain and is held at every level")
	{
		REQUIRE(child.Raise({ "k1", "dry-inbound-track", { { "trackId", "t1" } }, 100, std::nullopt }));
		REQUIRE_FALSE(child.Raise({ "k1", "dry-inbound-track", nullptr, 100, std::nullopt }));

		REQUIRE(child.Has("k1"));
		REQUIRE(parent.Has("k1"));
		REQUIRE(sink.raised.size() == 1);
		REQUIRE(sink.raised[0].raisedAt == 100);
		REQUIRE(parent.HasType("dry-inbound-track"));
	}

	SECTION("an update merges the payload")
	{
		child.Raise({ "k1", "t", { { "a", 1 } }, 100, std::nullopt });
		REQUIRE(child.Update({ "k1", { { "b", 2 } }, 200, std::nullopt }));

		REQUIRE(sink.updated.size() == 1);
		REQUIRE(sink.updated[0].payload["a"] == 1);
		REQUIRE(sink.updated[0].payload["b"] == 2);
		REQUIRE(sink.updated[0].updatedAt == 200);
	}

	SECTION("resolve drops the issue everywhere and carries the comment")
	{
		child.Raise({ "k1", "t", { { "a", 1 } }, 100, std::nullopt });

		const auto resolved =
		  child.Resolve({ "k1", std::string("recovered"), { { "durationInMs", 5 } }, 300 });

		REQUIRE(resolved.has_value());
		REQUIRE(resolved->comment == std::string("recovered"));
		REQUIRE(resolved->payload["a"] == 1);
		REQUIRE(resolved->payload["durationInMs"] == 5);
		REQUIRE_FALSE(child.Has("k1"));
		REQUIRE_FALSE(parent.Has("k1"));
		REQUIRE(sink.resolved.size() == 1);
		REQUIRE_FALSE(child.Resolve({ "k1", std::nullopt, nullptr, std::nullopt }).has_value());
	}

	SECTION("ResolveAll resolves in raise order")
	{
		child.Raise({ "b", "t", nullptr, 1, std::nullopt });
		child.Raise({ "a", "t", nullptr, 2, std::nullopt });

		REQUIRE(child.ResolveAll("gone") == 2);
		REQUIRE(sink.resolved[0].key == "b");
		REQUIRE(sink.resolved[1].key == "a");
	}

	SECTION("notify passes a one-shot issue straight through")
	{
		child.Notify({ "user-reported-bad-audio", { { "rating", 2 } }, 5, std::nullopt });

		REQUIRE(sink.notified.size() == 1);
		REQUIRE(child.GetSize() == 0);
	}
}
