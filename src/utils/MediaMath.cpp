#include "clientmonitor/utils/MediaMath.hpp"
#include "clientmonitor/utils/Utils.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace observertc::clientmonitor
{
	namespace utils
	{
		namespace
		{
			std::string ToLower(std::string value)
			{
				std::transform(
				  value.begin(),
				  value.end(),
				  value.begin(),
				  [](unsigned char c)
				  {
					  return std::tolower(c);
				  });

				return value;
			}

			double MosOfR(double r)
			{
				return 1 + (0.035 * r) + (0.000007 * r * (r - 60) * (100 - r));
			}

			const double RBest            = 93.2 - (10.0 / 40.0);
			const double TransportMosBest = MosOfR(RBest);
			const double TransportMosWorst{ 1 };
		} // namespace

		std::optional<double> QpScaleOf(const std::optional<std::string>& mimeType)
		{
			static const std::map<std::string, double> QpScaleByCodec = {
				{ "vp8",  127 },
        { "vp9",  255 },
        { "av1",  255 },
				{ "h264", 51  },
        { "h265", 51  },
        { "hevc", 51  },
			};

			if (!mimeType.has_value())
			{
				return std::nullopt;
			}

			const auto slash = mimeType->find('/');

			if (slash == std::string::npos)
			{
				return std::nullopt;
			}

			std::string codec;

			for (char c : ToLower(mimeType->substr(slash + 1)))
			{
				if (std::isalnum(static_cast<unsigned char>(c)))
				{
					codec.push_back(c);
				}
			}

			auto it = QpScaleByCodec.find(codec);

			return it == QpScaleByCodec.end() ? std::nullopt : std::optional<double>(it->second);
		}

		bool RunsOffCpu(
		  const std::optional<std::string>& implementation, const std::optional<bool>& powerEfficient)
		{
			static const std::vector<std::string> OffCpuMarkers = {
				"accelerator",     "external",     "hardware", "mediacodec",
				"mediafoundation", "videotoolbox", "vaapi",    "nvenc",
				"nvdec",           "quicksync",    "omx",
			};

			if (powerEfficient.value_or(false))
			{
				return true;
			}
			if (!implementation.has_value())
			{
				return false;
			}

			const auto name = ToLower(*implementation);

			return std::any_of(
			  OffCpuMarkers.begin(),
			  OffCpuMarkers.end(),
			  [&name](const std::string& marker)
			  {
				  return name.find(marker) != std::string::npos;
			  });
		}

		double TransportStability(double rttInMs, double jitterInMs, double packetLossPercent)
		{
			const double effectiveLatencyInMs = (rttInMs / 2) + (jitterInMs * 2) + 10;
			const double rLatency             = effectiveLatencyInMs < 160
			                                      ? 93.2 - (effectiveLatencyInMs / 40)
			                                      : 93.2 - ((effectiveLatencyInMs - 120) / 10);
			const double r                    = Clamp(rLatency - (packetLossPercent * 2.5), 0, 100);
			const double mos                  = Clamp(MosOfR(r), TransportMosWorst, TransportMosBest);

			return Clamp((mos - TransportMosWorst) / (TransportMosBest - TransportMosWorst), 0, 1);
		}

		std::optional<std::map<std::string, double>> SampledScoreReasons(
		  const std::map<std::string, double>& reasons, bool sendScoreReasonsToServer)
		{
			if (!sendScoreReasonsToServer || reasons.empty())
			{
				return std::nullopt;
			}

			return reasons;
		}
	} // namespace utils
} // namespace observertc::clientmonitor
