#include "clientmonitor/utils/Utils.hpp"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <random>

namespace observertc::clientmonitor
{
	namespace utils
	{
		namespace
		{
			std::mutex clockMutex;
			std::function<int64_t()> customClock;
		} // namespace

		int64_t NowMs()
		{
			{
				std::lock_guard<std::mutex> lock(clockMutex);

				if (customClock)
				{
					return customClock();
				}
			}

			return std::chrono::duration_cast<std::chrono::milliseconds>(
			         std::chrono::system_clock::now().time_since_epoch())
			  .count();
		}

		void SetClock(std::function<int64_t()> clock)
		{
			std::lock_guard<std::mutex> lock(clockMutex);

			customClock = std::move(clock);
		}

		std::string CreateUuid()
		{
			static thread_local std::mt19937_64 engine{ std::random_device{}() };
			std::uniform_int_distribution<uint64_t> distribution;

			uint64_t high = distribution(engine);
			uint64_t low  = distribution(engine);

			// Version 4, variant 10xx.
			high = (high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
			low  = (low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

			char buffer[37];

			std::snprintf(
			  buffer,
			  sizeof(buffer),
			  "%08x-%04x-%04x-%04x-%012llx",
			  static_cast<unsigned int>(high >> 32),
			  static_cast<unsigned int>((high >> 16) & 0xFFFF),
			  static_cast<unsigned int>(high & 0xFFFF),
			  static_cast<unsigned int>(low >> 48),
			  static_cast<unsigned long long>(low & 0xFFFFFFFFFFFFULL));

			return buffer;
		}
	} // namespace utils
} // namespace observertc::clientmonitor
