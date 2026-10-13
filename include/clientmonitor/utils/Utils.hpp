#ifndef CM_UTILS_UTILS_HPP
#define CM_UTILS_UTILS_HPP

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace observertc::clientmonitor
{
	namespace utils
	{
		/**
		 * Wall-clock milliseconds since the epoch (the JS library's `Date.now()`).
		 *
		 * Overridable so tests can drive time deterministically. SetClock(nullptr) restores the
		 * system clock.
		 */
		int64_t NowMs();
		void SetClock(std::function<int64_t()> clock);

		/** A random RFC 4122 v4 UUID, used when an application does not name a peer connection. */
		std::string CreateUuid();

		/**
		 * `current - previous`, or nullopt when either side is missing or the counter went
		 * backwards (a restart, whose difference is not a quantity of anything).
		 */
		template<typename A, typename B>
		std::optional<double> PositiveDelta(const std::optional<A>& current, const std::optional<B>& previous)
		{
			if (!current.has_value() || !previous.has_value())
			{
				return std::nullopt;
			}

			const auto c = static_cast<double>(*current);
			const auto p = static_cast<double>(*previous);

			if (c < p)
			{
				return std::nullopt;
			}

			return c - p;
		}

		/** Sums two optionals, treating a missing side as "nothing to add" rather than zero. */
		inline std::optional<double> AccumulatedValue(
		  const std::optional<double>& current, const std::optional<double>& value)
		{
			if (!current.has_value())
			{
				return value;
			}
			if (!value.has_value())
			{
				return current;
			}

			return *current + *value;
		}

		template<typename T>
		std::optional<double> ToDouble(const std::optional<T>& value)
		{
			if (!value.has_value())
			{
				return std::nullopt;
			}

			return static_cast<double>(*value);
		}

		inline double Clamp(double value, double min, double max)
		{
			return std::min(max, std::max(min, value));
		}
	} // namespace utils
} // namespace observertc::clientmonitor

#endif
