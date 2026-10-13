#include "clientmonitor/schema/SchemaTypes.hpp"

#include <catch.hpp>

using json = nlohmann::json;
using namespace observertc::clientmonitor;

TEST_CASE("schema", "[schema]")
{
	SECTION("FromJson reads numbers given as strings, as older libwebrtc builds serialise 64-bit members")
	{
		const auto stats = OutboundRtpStats::FromJson({
		  { "id",        "O1"    },
		  { "timestamp", 1000.5  },
		  { "ssrc",      "2222"  },
		  { "kind",      "video" },
		  { "bytesSent", "12345" }
    });

		REQUIRE(stats.id == "O1");
		REQUIRE(stats.timestamp == Approx(1000.5));
		REQUIRE(stats.ssrc == 2222);
		REQUIRE(stats.bytesSent == 12345);
		REQUIRE_FALSE(stats.packetsSent.has_value());
	}

	SECTION("a field of the wrong type reads as absent")
	{
		const auto stats = InboundRtpStats::FromJson({
		  { "id",		          "I1"           },
      { "packetsReceived", "not-a-number" }
    });

		REQUIRE_FALSE(stats.packetsReceived.has_value());
	}

	SECTION("ToJson leaves out what was never reported and round-trips the rest")
	{
		OutboundRtpStats stats;

		stats.id                         = "O1";
		stats.timestamp                  = 1000;
		stats.ssrc                       = 1;
		stats.kind                       = "video";
		stats.bytesSent                  = 10;
		stats.qualityLimitationDurations = QualityLimitationDurations{ 1, 2, 3, 4 };

		const auto j = stats.ToJson();

		REQUIRE(j["bytesSent"] == 10);
		REQUIRE_FALSE(j.contains("packetsSent"));
		REQUIRE(j["qualityLimitationDurations"]["bandwidth"] == 3);

		const auto back = OutboundRtpStats::FromJson(j);

		REQUIRE(back.bytesSent == 10);
		REQUIRE(back.qualityLimitationDurations->other == 4);
	}

	SECTION("a ClientSample carries nested payloads as records")
	{
		ClientSample sample;
		ClientEvent event;

		event.type    = "CLIENT_JOINED";
		event.payload = {
			{ "nested", { { "a", 1 } } }
		};
		event.timestamp = 1;

		sample.timestamp = 5;
		sample.clientEvents = std::vector<ClientEvent>{ event };

		const auto j = sample.ToJson();

		REQUIRE(j["clientEvents"][0]["payload"]["nested"]["a"] == 1);
		// An unset optional list is left out; the monitor always sets the lists it samples.
		REQUIRE_FALSE(j.contains("peerConnections"));
		REQUIRE(observertc::schemas::SchemaVersion == std::string("3.7.0"));
	}
}
