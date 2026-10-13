#include "clientmonitor/StatsCollector.hpp"
#include "helpers.hpp"

#include <catch.hpp>

using json = nlohmann::json;
using namespace observertc::clientmonitor;

namespace
{
	const json* Find(const json& stats, const std::string& id)
	{
		for (const auto& stat : stats)
		{
			if (stat["id"] == id)
			{
				return &stat;
			}
		}

		return nullptr;
	}
} // namespace

TEST_CASE("LibWebRtcStatsAdapter", "[adapters]")
{
	LibWebRtcStatsAdapter adapter;
	helpers::Counters counters;
	auto stats = helpers::BuildReport(counters);

	adapter.Adapt(stats);

	SECTION("folds mediaType into kind and ip into address")
	{
		REQUIRE((*Find(stats, "IA1111"))["kind"] == "audio");
		REQUIRE_FALSE(Find(stats, "IA1111")->contains("mediaType"));
		REQUIRE((*Find(stats, "L1"))["address"] == "10.0.0.1");
	}

	SECTION("infers the references a single candidate makes unambiguous")
	{
		REQUIRE((*Find(stats, "OV2222"))["mediaSourceId"] == "MS1");
		REQUIRE((*Find(stats, "OV2222"))["transportId"] == "T01");
		REQUIRE((*Find(stats, "OV2222"))["remoteId"] == "RIV2222");
		REQUIRE((*Find(stats, "RIV2222"))["localId"] == "OV2222");
		REQUIRE((*Find(stats, "RIV2222"))["codecId"] == "CV");
	}

	SECTION("folds legacy track reports into inbound-rtp")
	{
		json legacy = json::array({
		  { { "type", "track" }, { "id", "TR1" }, { "trackIdentifier", "abc" }, { "framesReceived", 10 } },
		  { { "type", "inbound-rtp" }, { "id", "I1" }, { "ssrc", 1 }, { "kind", "video" }, { "trackId", "TR1" } },
		  { { "type", "stream" }, { "id", "S1" } },
		});

		adapter.Adapt(legacy);

		REQUIRE(legacy.size() == 1);
		REQUIRE(legacy[0]["trackIdentifier"] == "abc");
		REQUIRE(legacy[0]["framesReceived"] == 10);
		REQUIRE_FALSE(legacy[0].contains("trackId"));
	}
}
