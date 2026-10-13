#ifndef CM_UTILS_MEDIA_MATH_HPP
#define CM_UTILS_MEDIA_MATH_HPP

#include <map>
#include <optional>
#include <string>

namespace clientmonitor
{
	namespace utils
	{
		/**
		 * The top of a codec's quantizer scale (VP8 127, VP9/AV1 255, H.264/H.265 51), read from a
		 * mime type such as "video/VP8". Nullopt for a codec without a known scale.
		 */
		std::optional<double> QpScaleOf(const std::optional<std::string>& mimeType);

		/**
		 * Whether an encoder or decoder runs off the CPU (hardware, an accelerator, a platform
		 * framework), so its time does not count against the CPU budget.
		 */
		bool RunsOffCpu(
		  const std::optional<std::string>& implementation, const std::optional<bool>& powerEfficient);

		/**
		 * One conversational reading of the transport from its round trip, jitter and loss, in
		 * [0, 1], from the E-model's MOS. Port of the JS `transportStability`.
		 */
		double TransportStability(double rttInMs, double jitterInMs, double packetLossPercent);

		/** Score reasons as they go into the sample: nullopt when disabled or empty. */
		std::optional<std::map<std::string, double>> SampledScoreReasons(
		  const std::map<std::string, double>& reasons, bool sendScoreReasonsToServer);
	} // namespace utils
} // namespace clientmonitor

#endif
