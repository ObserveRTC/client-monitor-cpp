#ifndef CM_UTILS_SLICED_WINDOW_HPP
#define CM_UTILS_SLICED_WINDOW_HPP

#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace clientmonitor
{
	/**
	 * A rolling buffer of running totals with a fixed set of named slices over it, each reporting
	 * how far every total moved across the stretch it covers. Port of the JS `SlicedWindow`.
	 *
	 * Totals are addressed by index: an owner declares an enum of its totals and passes its size
	 * as `numberOfTotals`. Feed it cumulative counters, not per-collection deltas. A delta is the
	 * difference between the first and last value a slice covers, so it spans exactly the stretch
	 * its `GetDurationInMs()` measures.
	 *
	 * Values must be added in non-decreasing timestamp order.
	 */
	class SlicedWindow
	{
	public:
		/** The fewest values a slice can cover, and so the smallest one that measures anything. */
		static constexpr size_t MinSamples{ 2 };

		struct SliceConfig
		{
			// How many consecutive values this slice covers. At least 2.
			size_t numberOfSamples{ 2 };
			// How many values back from the newest the slice begins. 0 is the newest value.
			size_t offset{ 0 };
		};

		struct Config
		{
			size_t numberOfTotals{ 0 };
			// Milliseconds between two consecutive values above which the window resets.
			double maxAllowedGapInMs{ 20000 };
			// Defaults to the furthest reach of the slices.
			std::optional<size_t> capacity;
			std::map<std::string, SliceConfig> slices;
		};

		using Values = std::vector<std::optional<double>>;

		class Slice
		{
		public:
			Slice(size_t numberOfSamples, size_t offset, size_t numberOfTotals);

		public:
			/** Whether the window holds every value this slice was asked to cover. */
			bool IsReady() const;
			/** Milliseconds between the oldest and newest value this slice covers. */
			double GetDurationInMs() const;
			/** How many values the slice is covering right now. */
			size_t GetNumberOfEntries() const;
			size_t GetNumberOfSamples() const;
			size_t GetOffset() const;
			/** How far the total at `index` moved across the slice, or nullopt if unmeasurable. */
			std::optional<double> GetDelta(size_t index) const;

		private:
			friend class SlicedWindow;

			size_t numberOfSamples;
			size_t offset;
			size_t numberOfEntries{ 0 };
			double durationInMs{ 0 };
			std::vector<std::optional<double>> deltas;
		};

	public:
		explicit SlicedWindow(const Config& config);

	public:
		/** Throws std::invalid_argument on a non-finite or decreasing timestamp. */
		void Add(double timestamp, const Values& values);
		void Reset();
		/** The slice declared under this name. Throws std::out_of_range for an undeclared one. */
		const Slice& GetSlice(const std::string& name) const;
		bool HasSlice(const std::string& name) const;
		size_t GetCapacity() const;
		size_t GetNumberOfEntries() const;

	private:
		struct Entry
		{
			double timestamp;
			Values values;
		};

		void RefreshSlices();
		void RefreshSlice(Slice& slice);

	private:
		Config config;
		size_t capacity{ MinSamples };
		std::deque<Entry> entries;
		std::map<std::string, std::unique_ptr<Slice>> slices;
		std::optional<double> lastTimestamp;
	};
} // namespace clientmonitor

#endif
