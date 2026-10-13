#ifndef CM_UTILS_JSON_READER_HPP
#define CM_UTILS_JSON_READER_HPP

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace clientmonitor
{
	namespace detail
	{
		/**
		 * Tolerant readers for W3C stats coming from libwebrtc's RTCStatsReport::ToJson().
		 *
		 * libwebrtc serialises 64-bit members as doubles (and older builds as strings), so a
		 * number is accepted in either form. A value of the wrong type reads as absent, the same
		 * way the JS library treats an unexpected `undefined`.
		 */
		inline std::optional<double> ReadDouble(const nlohmann::json& j, const char* key)
		{
			auto it = j.find(key);

			if (it == j.end() || it->is_null())
			{
				return std::nullopt;
			}
			if (it->is_number())
			{
				const double value = it->get<double>();

				return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
			}
			if (it->is_string())
			{
				try
				{
					size_t pos{ 0 };
					const auto& str    = it->get_ref<const std::string&>();
					const double value = std::stod(str, &pos);

					if (pos == str.size() && std::isfinite(value))
					{
						return value;
					}
				}
				catch (...)
				{
				}
			}

			return std::nullopt;
		}

		inline std::optional<int64_t> ReadInt(const nlohmann::json& j, const char* key)
		{
			auto it = j.find(key);

			if (it != j.end() && (it->is_number_integer() || it->is_number_unsigned()))
			{
				return it->get<int64_t>();
			}

			auto value = ReadDouble(j, key);

			if (!value)
			{
				return std::nullopt;
			}

			return static_cast<int64_t>(*value);
		}

		inline std::optional<bool> ReadBool(const nlohmann::json& j, const char* key)
		{
			auto it = j.find(key);

			if (it == j.end() || !it->is_boolean())
			{
				return std::nullopt;
			}

			return it->get<bool>();
		}

		inline std::optional<std::string> ReadString(const nlohmann::json& j, const char* key)
		{
			auto it = j.find(key);

			if (it == j.end() || !it->is_string())
			{
				return std::nullopt;
			}

			return it->get<std::string>();
		}

		inline std::optional<std::map<std::string, double>> ReadDoubleMap(
		  const nlohmann::json& j, const char* key)
		{
			auto it = j.find(key);

			if (it == j.end() || !it->is_object())
			{
				return std::nullopt;
			}

			std::map<std::string, double> result;

			for (const auto& [name, value] : it->items())
			{
				if (value.is_number())
				{
					result[name] = value.get<double>();
				}
			}

			return result;
		}
		/** A whole number as an integer, anything else as a double. */
		inline nlohmann::json WholeOrFraction(double value)
		{
			if (std::isfinite(value) && std::floor(value) == value && std::abs(value) < 9007199254740992.0)
			{
				return static_cast<int64_t>(value);
			}

			return value;
		}
	} // namespace detail
} // namespace clientmonitor

#endif
