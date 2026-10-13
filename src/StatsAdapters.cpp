#define CM_CLASS "LibWebRtcStatsAdapter"

#include "clientmonitor/Logger.hpp"
#include "clientmonitor/StatsCollector.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <vector>

using json = nlohmann::json;

namespace observertc::clientmonitor
{
	namespace
	{
		bool IsType(const json& stat, const char* type)
		{
			auto it = stat.find("type");

			return it != stat.end() && it->is_string() && it->get_ref<const std::string&>() == type;
		}

		bool Has(const json& stat, const char* field)
		{
			auto it = stat.find(field);

			return it != stat.end() && !it->is_null();
		}

		bool IsRtpStream(const json& stat)
		{
			return IsType(stat, "inbound-rtp") || IsType(stat, "outbound-rtp") ||
			       IsType(stat, "remote-inbound-rtp") || IsType(stat, "remote-outbound-rtp");
		}

		void FoldField(json& stat, const char* legacyField, const char* standardField)
		{
			if (!Has(stat, legacyField))
			{
				return;
			}
			if (!Has(stat, standardField))
			{
				stat[standardField] = stat[legacyField];
			}

			stat.erase(legacyField);
		}

		std::string SsrcKey(const json& stat)
		{
			const auto& ssrc = stat["ssrc"];

			return stat["type"].get<std::string>() + ":" +
			       (ssrc.is_string() ? ssrc.get<std::string>() : ssrc.dump());
		}

		/** Folds the deprecated `track` reports into the rtp stream that references them. */
		void FoldLegacyTrackStats(json& stats)
		{
			static const std::vector<const char*> TrackToInboundFields = {
				"trackIdentifier",      "framesReceived",
				"framesDropped",        "frameWidth",
				"frameHeight",          "freezeCount",
				"totalFreezesDuration", "pauseCount",
				"totalPausesDuration",  "audioLevel",
				"totalAudioEnergy",     "totalSamplesDuration",
				"jitterBufferDelay",    "jitterBufferEmittedCount",
			};

			std::map<std::string, json> trackReports;

			for (const auto& stat : stats)
			{
				if (IsType(stat, "track") && Has(stat, "id"))
				{
					trackReports[stat["id"].get<std::string>()] = stat;
				}
			}

			if (!trackReports.empty())
			{
				for (auto& stat : stats)
				{
					if (!Has(stat, "trackId"))
					{
						continue;
					}
					if (!IsType(stat, "inbound-rtp") && !IsType(stat, "outbound-rtp"))
					{
						continue;
					}

					auto it =
					  trackReports.find(stat["trackId"].is_string() ? stat["trackId"].get<std::string>() : "");

					if (it != trackReports.end())
					{
						const auto& track = it->second;

						if (IsType(stat, "inbound-rtp"))
						{
							for (const auto* field : TrackToInboundFields)
							{
								if (!Has(stat, field) && Has(track, field))
								{
									stat[field] = track[field];
								}
							}
						}
						if (!Has(stat, "trackIdentifier") && Has(track, "trackIdentifier"))
						{
							stat["trackIdentifier"] = track["trackIdentifier"];
						}
					}

					stat.erase("trackId");
				}
			}

			json result = json::array();

			for (auto& stat : stats)
			{
				if (IsType(stat, "track") || IsType(stat, "stream"))
				{
					continue;
				}

				result.push_back(std::move(stat));
			}

			stats = std::move(result);
		}

		void InferOutboundMediaSourceId(json& stats)
		{
			std::vector<const json*> mediaSources;

			for (const auto& stat : stats)
			{
				if (IsType(stat, "media-source"))
				{
					mediaSources.push_back(&stat);
				}
			}

			for (auto& stat : stats)
			{
				if (!IsType(stat, "outbound-rtp") || Has(stat, "mediaSourceId"))
				{
					continue;
				}

				const json* source{ nullptr };

				if (Has(stat, "trackIdentifier"))
				{
					for (const auto* mediaSource : mediaSources)
					{
						if (Has(*mediaSource, "trackIdentifier") && (*mediaSource)["trackIdentifier"] == stat["trackIdentifier"])
						{
							source = mediaSource;
							break;
						}
					}
				}
				if (!source && Has(stat, "kind"))
				{
					std::vector<const json*> sameKind;

					for (const auto* mediaSource : mediaSources)
					{
						if (Has(*mediaSource, "kind") && (*mediaSource)["kind"] == stat["kind"])
						{
							sameKind.push_back(mediaSource);
						}
					}
					if (sameKind.size() == 1)
					{
						source = sameKind[0];
					}
				}
				if (source && Has(*source, "id"))
				{
					stat["mediaSourceId"] = (*source)["id"];
				}
			}
		}

		void InferTransportId(json& stats)
		{
			static const std::set<std::string> ReferringTypes = {
				"inbound-rtp", "outbound-rtp",   "remote-inbound-rtp", "remote-outbound-rtp",
				"codec",       "candidate-pair", "local-candidate",    "remote-candidate",
			};

			std::optional<json> transportId;

			for (const auto& stat : stats)
			{
				if (!IsType(stat, "transport"))
				{
					continue;
				}
				// More than one transport: ambiguous.
				if (transportId.has_value())
				{
					return;
				}

				transportId = stat["id"];
			}

			if (!transportId.has_value())
			{
				return;
			}

			for (auto& stat : stats)
			{
				if (Has(stat, "transportId") || !stat["type"].is_string())
				{
					continue;
				}
				if (ReferringTypes.count(stat["type"].get<std::string>()) == 0)
				{
					continue;
				}

				stat["transportId"] = *transportId;
			}
		}

		void InferRtpCrossReferences(json& stats)
		{
			struct CrossReference
			{
				const char* from;
				const char* to;
				const char* field;
			};

			static const std::vector<CrossReference> CrossReferences = {
				{ "outbound-rtp",        "remote-inbound-rtp",  "remoteId" },
				{ "remote-inbound-rtp",  "outbound-rtp",        "localId"  },
				{ "inbound-rtp",         "remote-outbound-rtp", "remoteId" },
				{ "remote-outbound-rtp", "inbound-rtp",         "localId"  },
			};

			std::map<std::string, std::vector<const json*>> bySsrc;

			for (const auto& stat : stats)
			{
				if (!IsRtpStream(stat) || !Has(stat, "ssrc"))
				{
					continue;
				}

				bySsrc[SsrcKey(stat)].push_back(&stat);
			}

			std::vector<std::pair<size_t, std::pair<const char*, json>>> assignments;

			for (size_t i = 0; i < stats.size(); ++i)
			{
				const auto& stat = stats[i];

				if (!IsRtpStream(stat) || !Has(stat, "ssrc"))
				{
					continue;
				}

				for (const auto& ref : CrossReferences)
				{
					if (!IsType(stat, ref.from) || Has(stat, ref.field))
					{
						continue;
					}

					json partnerKeyStat = {
						{ "type", ref.to       },
            { "ssrc", stat["ssrc"] }
					};
					auto it = bySsrc.find(SsrcKey(partnerKeyStat));

					if (it != bySsrc.end() && it->second.size() == 1 && Has(*it->second[0], "id"))
					{
						assignments.push_back({
						  i, { ref.field, (*it->second[0])["id"] }
            });
					}
				}
			}

			for (auto& [index, assignment] : assignments)
			{
				stats[index][assignment.first] = assignment.second;
			}
		}

		void InferCodecId(json& stats)
		{
			std::vector<size_t> codecs;

			for (size_t i = 0; i < stats.size(); ++i)
			{
				if (IsType(stats[i], "codec"))
				{
					codecs.push_back(i);
				}
			}

			for (auto& stat : stats)
			{
				if (!IsRtpStream(stat) || Has(stat, "codecId") || !Has(stat, "kind") || !stat["kind"].is_string())
				{
					continue;
				}

				const auto prefix = stat["kind"].get<std::string>() + "/";
				std::vector<const json*> candidates;

				for (auto index : codecs)
				{
					const auto& codec = stats[index];

					if (!Has(codec, "mimeType") || !codec["mimeType"].is_string())
					{
						continue;
					}

					auto mimeType = codec["mimeType"].get<std::string>();

					std::transform(
					  mimeType.begin(),
					  mimeType.end(),
					  mimeType.begin(),
					  [](unsigned char c)
					  {
						  return std::tolower(c);
					  });

					if (mimeType.rfind(prefix, 0) == 0)
					{
						candidates.push_back(&codec);
					}
				}

				if (Has(stat, "transportId"))
				{
					std::vector<const json*> sameTransport;

					for (const auto* codec : candidates)
					{
						if (!Has(*codec, "transportId") || (*codec)["transportId"] == stat["transportId"])
						{
							sameTransport.push_back(codec);
						}
					}
					if (!sameTransport.empty())
					{
						candidates = sameTransport;
					}
				}

				const bool directional = std::any_of(
				  candidates.begin(),
				  candidates.end(),
				  [](const json* codec)
				  {
					  return Has(*codec, "codecType");
				  });

				if (directional)
				{
					const std::string wanted =
					  IsType(stat, "outbound-rtp") || IsType(stat, "remote-inbound-rtp") ? "encode" : "decode";
					std::vector<const json*> sameDirection;

					for (const auto* codec : candidates)
					{
						if (!Has(*codec, "codecType") || (*codec)["codecType"] == wanted)
						{
							sameDirection.push_back(codec);
						}
					}
					if (!sameDirection.empty())
					{
						candidates = sameDirection;
					}
				}

				if (candidates.size() == 1 && Has(*candidates[0], "id"))
				{
					stat["codecId"] = (*candidates[0])["id"];
				}
			}
		}
	} // namespace

	const std::string& LibWebRtcStatsAdapter::GetName() const
	{
		static const std::string Name{ "libWebRtcStatsAdapter" };

		return Name;
	}

	void LibWebRtcStatsAdapter::Adapt(json& stats)
	{
		CM_TRACE();

		if (!stats.is_array())
		{
			return;
		}

		FoldLegacyTrackStats(stats);

		for (auto& stat : stats)
		{
			if (!stat.is_object() || !stat.contains("type") || !stat["type"].is_string())
			{
				continue;
			}
			if (IsRtpStream(stat))
			{
				FoldField(stat, "mediaType", "kind");

				continue;
			}
			if (IsType(stat, "local-candidate") || IsType(stat, "remote-candidate"))
			{
				FoldField(stat, "ip", "address");
			}
		}

		InferOutboundMediaSourceId(stats);
		InferTransportId(stats);
		InferRtpCrossReferences(stats);
		InferCodecId(stats);
	}
} // namespace observertc::clientmonitor
