#include "clientmonitor/utils/SlicedWindow.hpp"

#include <catch.hpp>

#include <stdexcept>

using namespace clientmonitor;

namespace
{
	SlicedWindow::Config MakeConfig()
	{
		SlicedWindow::Config config;

		config.numberOfTotals      = 2;
		config.maxAllowedGapInMs   = 10000;
		config.slices["detection"] = { 3, 0 };
		config.slices["recovery"]  = { 3, 2 };

		return config;
	}
} // namespace

TEST_CASE("SlicedWindow", "[utils]")
{
	SECTION("capacity defaults to the furthest reach of the slices")
	{
		SlicedWindow window(MakeConfig());

		REQUIRE(window.GetCapacity() == 5);
	}

	SECTION("a slice reports endpoint deltas once it covers its samples")
	{
		SlicedWindow window(MakeConfig());
		const auto& detection = window.GetSlice("detection");

		window.Add(0, { 0.0, 10.0 });
		window.Add(1000, { 5.0, 20.0 });

		REQUIRE_FALSE(detection.IsReady());
		REQUIRE_FALSE(detection.GetDelta(0).has_value());

		window.Add(2000, { 12.0, 30.0 });

		REQUIRE(detection.IsReady());
		REQUIRE(detection.GetDurationInMs() == 2000);
		REQUIRE(*detection.GetDelta(0) == 12);
		REQUIRE(*detection.GetDelta(1) == 20);
	}

	SECTION("the recovery slice sits behind the detection slice")
	{
		SlicedWindow window(MakeConfig());

		for (int i = 0; i < 5; ++i)
		{
			window.Add(i * 1000.0, { i * 10.0, std::nullopt });
		}

		const auto& recovery = window.GetSlice("recovery");

		REQUIRE(recovery.IsReady());
		REQUIRE(*recovery.GetDelta(0) == 20);
		REQUIRE_FALSE(recovery.GetDelta(1).has_value());
	}

	SECTION("a counter that went backwards reads as unmeasurable")
	{
		SlicedWindow window(MakeConfig());

		window.Add(0, { 100.0, 0.0 });
		window.Add(1000, { 50.0, 1.0 });
		window.Add(2000, { 60.0, 2.0 });

		REQUIRE_FALSE(window.GetSlice("detection").GetDelta(0).has_value());
		REQUIRE(*window.GetSlice("detection").GetDelta(1) == 2);
	}

	SECTION("a gap wider than maxAllowedGapInMs starts the fill again")
	{
		SlicedWindow window(MakeConfig());

		window.Add(0, { 0.0, 0.0 });
		window.Add(1000, { 1.0, 1.0 });
		window.Add(2000, { 2.0, 2.0 });
		window.Add(20000, { 3.0, 3.0 });

		REQUIRE(window.GetNumberOfEntries() == 1);
		REQUIRE_FALSE(window.GetSlice("detection").IsReady());
	}

	SECTION("rejects decreasing timestamps and impossible slices")
	{
		SlicedWindow window(MakeConfig());

		window.Add(1000, { 0.0, 0.0 });

		REQUIRE_THROWS_AS(window.Add(500, { 0.0, 0.0 }), std::invalid_argument);

		auto config     = MakeConfig();
		config.capacity = 3;
		REQUIRE_THROWS_AS(SlicedWindow(config), std::invalid_argument);
	}
}
