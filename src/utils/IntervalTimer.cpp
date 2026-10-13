#include "clientmonitor/utils/IntervalTimer.hpp"

#include <chrono>

namespace clientmonitor
{
	IntervalTimer::~IntervalTimer()
	{
		this->Stop();
	}

	void IntervalTimer::Start(int64_t periodInMs, std::function<void()> callback)
	{
		this->Stop();

		if (periodInMs <= 0 || !callback)
		{
			return;
		}

		std::lock_guard<std::mutex> control(this->controlMutex);

		auto state  = std::make_shared<State>();
		this->state = state;

		this->thread = std::thread(
		  [state, periodInMs, callback = std::move(callback)]()
		  {
			  auto next = std::chrono::steady_clock::now() + std::chrono::milliseconds(periodInMs);

			  while (true)
			  {
				  {
					  std::unique_lock<std::mutex> lock(state->mutex);

					  if (state->cv.wait_until(
					        lock,
					        next,
					        [&state]()
					        {
						        return state->stopped;
					        }))
					  {
						  return;
					  }
				  }

				  callback();

				  {
					  std::lock_guard<std::mutex> lock(state->mutex);

					  if (state->stopped)
					  {
						  return;
					  }
				  }

				  next += std::chrono::milliseconds(periodInMs);

				  // A callback that overran the period does not get to queue up a burst of catch-up runs.
				  const auto now = std::chrono::steady_clock::now();

				  if (next < now)
				  {
					  next = now + std::chrono::milliseconds(periodInMs);
				  }
			  }
		  });
	}

	void IntervalTimer::Stop()
	{
		std::thread thread;

		{
			std::lock_guard<std::mutex> control(this->controlMutex);

			if (this->state)
			{
				std::lock_guard<std::mutex> lock(this->state->mutex);

				this->state->stopped = true;
				this->state->cv.notify_all();
			}

			this->state.reset();
			thread = std::move(this->thread);
		}

		// Joined outside the control lock, so the timer thread can still reach Stop() meanwhile.
		if (thread.joinable())
		{
			if (thread.get_id() == std::this_thread::get_id())
			{
				thread.detach();
			}
			else
			{
				thread.join();
			}
		}
	}

	bool IntervalTimer::IsRunning() const
	{
		std::lock_guard<std::mutex> control(this->controlMutex);

		return this->thread.joinable();
	}
} // namespace clientmonitor
