#include "clientmonitor/utils/SlicedWindow.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace observertc::clientmonitor
{
	namespace
	{
		/**
		 * How far one total moved between two endpoints. Nullopt where either endpoint did not
		 * report it, or where it went backwards (the counter restarted).
		 */
		std::optional<double> EndpointDelta(
		  const std::optional<double>& from, const std::optional<double>& to)
		{
			if (!from.has_value() || !to.has_value())
			{
				return std::nullopt;
			}
			if (*to < *from)
			{
				return std::nullopt;
			}

			return *to - *from;
		}
	} // namespace

	/* Slice */

	SlicedWindow::Slice::Slice(size_t numberOfSamples, size_t offset, size_t numberOfTotals)
	  : numberOfSamples(numberOfSamples), offset(offset), deltas(numberOfTotals)
	{
	}

	bool SlicedWindow::Slice::IsReady() const
	{
		return this->numberOfEntries == this->numberOfSamples;
	}

	double SlicedWindow::Slice::GetDurationInMs() const
	{
		return this->durationInMs;
	}

	size_t SlicedWindow::Slice::GetNumberOfEntries() const
	{
		return this->numberOfEntries;
	}

	size_t SlicedWindow::Slice::GetNumberOfSamples() const
	{
		return this->numberOfSamples;
	}

	size_t SlicedWindow::Slice::GetOffset() const
	{
		return this->offset;
	}

	std::optional<double> SlicedWindow::Slice::GetDelta(size_t index) const
	{
		if (this->deltas.size() <= index)
		{
			return std::nullopt;
		}

		return this->deltas[index];
	}

	/* SlicedWindow */

	SlicedWindow::SlicedWindow(const Config& config) : config(config)
	{
		if (config.capacity.has_value() && *config.capacity < MinSamples)
		{
			throw std::invalid_argument("capacity must be at least 2");
		}
		if (!std::isfinite(config.maxAllowedGapInMs) || config.maxAllowedGapInMs <= 0)
		{
			throw std::invalid_argument("maxAllowedGapInMs must be a positive finite number");
		}

		if (config.capacity.has_value())
		{
			this->capacity = *config.capacity;
		}
		else
		{
			this->capacity = MinSamples;

			for (const auto& [name, slice] : config.slices)
			{
				this->capacity = std::max(this->capacity, slice.offset + slice.numberOfSamples);
			}
		}

		for (const auto& [name, sliceConfig] : config.slices)
		{
			if (sliceConfig.numberOfSamples < MinSamples)
			{
				throw std::invalid_argument("slice \"" + name + "\": numberOfSamples must be at least 2");
			}
			// A slice reaching past the buffer could never fill. Refused rather than accepted.
			if (this->capacity < sliceConfig.offset + sliceConfig.numberOfSamples)
			{
				throw std::invalid_argument("slice \"" + name + "\" reaches past the window's capacity");
			}

			auto slice = std::make_unique<Slice>(
			  sliceConfig.numberOfSamples, sliceConfig.offset, config.numberOfTotals);

			this->RefreshSlice(*slice);
			this->slices.emplace(name, std::move(slice));
		}
	}

	void SlicedWindow::Add(double timestamp, const Values& values)
	{
		if (!std::isfinite(timestamp))
		{
			throw std::invalid_argument("timestamp must be a finite number");
		}
		if (this->lastTimestamp.has_value() && timestamp < *this->lastTimestamp)
		{
			throw std::invalid_argument("items must be added in non-decreasing timestamp order");
		}
		// A collection that did not move the clock measured no interval.
		if (this->lastTimestamp.has_value() && timestamp == *this->lastTimestamp)
		{
			return;
		}
		// Everything held describes a stretch that this value no longer continues.
		if (this->lastTimestamp.has_value() && this->config.maxAllowedGapInMs < timestamp - *this->lastTimestamp)
		{
			this->Reset();
		}

		this->lastTimestamp = timestamp;

		Entry entry{ timestamp, values };

		entry.values.resize(this->config.numberOfTotals);
		this->entries.push_back(std::move(entry));

		while (this->capacity < this->entries.size())
		{
			this->entries.pop_front();
		}

		this->RefreshSlices();
	}

	void SlicedWindow::Reset()
	{
		this->entries.clear();
		this->lastTimestamp.reset();

		this->RefreshSlices();
	}

	const SlicedWindow::Slice& SlicedWindow::GetSlice(const std::string& name) const
	{
		return *this->slices.at(name);
	}

	bool SlicedWindow::HasSlice(const std::string& name) const
	{
		return this->slices.find(name) != this->slices.end();
	}

	size_t SlicedWindow::GetCapacity() const
	{
		return this->capacity;
	}

	size_t SlicedWindow::GetNumberOfEntries() const
	{
		return this->entries.size();
	}

	void SlicedWindow::RefreshSlices()
	{
		for (auto& [name, slice] : this->slices)
		{
			this->RefreshSlice(*slice);
		}
	}

	void SlicedWindow::RefreshSlice(Slice& slice)
	{
		const auto size = static_cast<long long>(this->entries.size());
		// The newest value the slice covers, and the oldest, counted back from the newest held.
		const auto newest = size - 1 - static_cast<long long>(slice.offset);
		const auto oldest = size - static_cast<long long>(slice.offset + slice.numberOfSamples);
		const Entry* from = 0 <= oldest ? &this->entries[oldest] : nullptr;
		const Entry* to   = 0 <= newest ? &this->entries[newest] : nullptr;

		if (!from || !to)
		{
			const auto available = std::max<long long>(0, size - static_cast<long long>(slice.offset));

			slice.numberOfEntries = static_cast<size_t>(
			  std::min<long long>(static_cast<long long>(slice.numberOfSamples), available));
			slice.durationInMs = 0;

			std::fill(slice.deltas.begin(), slice.deltas.end(), std::nullopt);

			return;
		}

		slice.numberOfEntries = slice.numberOfSamples;
		slice.durationInMs    = to->timestamp - from->timestamp;

		for (size_t i = 0; i < slice.deltas.size(); ++i)
		{
			slice.deltas[i] = EndpointDelta(from->values[i], to->values[i]);
		}
	}
} // namespace observertc::clientmonitor
