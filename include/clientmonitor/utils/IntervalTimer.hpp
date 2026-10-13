#ifndef CM_UTILS_INTERVAL_TIMER_HPP
#define CM_UTILS_INTERVAL_TIMER_HPP

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace observertc::clientmonitor
{
	/**
	 * Runs a callback every `periodInMs` on its own thread (the JS `setInterval`).
	 *
	 * Stop() may be called from inside the callback: the thread is then detached and exits on its
	 * own after the callback returns, touching only state it shares with nobody else.
	 */
	class IntervalTimer
	{
	public:
		IntervalTimer() = default;
		~IntervalTimer();

		IntervalTimer(const IntervalTimer&)            = delete;
		IntervalTimer& operator=(const IntervalTimer&) = delete;

	public:
		void Start(int64_t periodInMs, std::function<void()> callback);
		void Stop();
		bool IsRunning() const;

	private:
		struct State
		{
			std::mutex mutex;
			std::condition_variable cv;
			bool stopped{ false };
		};

		// Guards `state` and `thread`, which Start() and Stop() may touch from different threads.
		mutable std::mutex controlMutex;
		std::shared_ptr<State> state;
		std::thread thread;
	};
} // namespace observertc::clientmonitor

#endif
