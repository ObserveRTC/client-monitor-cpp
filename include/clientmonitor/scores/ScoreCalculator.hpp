#ifndef CM_SCORES_SCORE_CALCULATOR_HPP
#define CM_SCORES_SCORE_CALCULATOR_HPP

namespace observertc::clientmonitor
{
	// Fast forward declarations.
	class ClientMonitor;

	/**
	 * Turns the monitors' state into scores after every collection: writes each track's and peer
	 * connection's CalculatedScore and calls ClientMonitor::SetScore(). Port of the JS
	 * `ScoreCalculator` interface.
	 *
	 * The JS `DefaultScoreCalculator` is not ported yet; until it is, no calculator is installed
	 * and the client score stays at 5.0 unless the application installs its own.
	 */
	class ScoreCalculator
	{
	public:
		virtual ~ScoreCalculator()                  = default;
		virtual void Update(ClientMonitor* monitor) = 0;
	};
} // namespace observertc::clientmonitor

#endif
