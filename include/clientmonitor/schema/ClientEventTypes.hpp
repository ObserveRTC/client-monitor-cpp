#ifndef CM_SCHEMA_CLIENT_EVENT_TYPES_HPP
#define CM_SCHEMA_CLIENT_EVENT_TYPES_HPP

namespace observertc::clientmonitor
{
	/** The client event types the library reports. Port of the JS `ClientEventTypes`. */
	namespace ClientEventTypes
	{
		constexpr const char* CLIENT_JOINED{ "CLIENT_JOINED" };
		constexpr const char* CLIENT_LEFT{ "CLIENT_LEFT" };
		constexpr const char* PEER_CONNECTION_OPENED{ "PEER_CONNECTION_OPENED" };
		constexpr const char* PEER_CONNECTION_CLOSED{ "PEER_CONNECTION_CLOSED" };
		constexpr const char* MEDIA_TRACK_ADDED{ "MEDIA_TRACK_ADDED" };
		constexpr const char* MEDIA_TRACK_REMOVED{ "MEDIA_TRACK_REMOVED" };
		constexpr const char* MEDIA_TRACK_RESUMED{ "MEDIA_TRACK_RESUMED" };
		constexpr const char* MEDIA_TRACK_MUTED{ "MEDIA_TRACK_MUTED" };
		constexpr const char* MEDIA_TRACK_UNMUTED{ "MEDIA_TRACK_UNMUTED" };
		constexpr const char* ICE_GATHERING_STATE_CHANGED{ "ICE_GATHERING_STATE_CHANGED" };
		constexpr const char* PEER_CONNECTION_STATE_CHANGED{ "PEER_CONNECTION_STATE_CHANGED" };
		constexpr const char* ICE_CONNECTION_STATE_CHANGED{ "ICE_CONNECTION_STATE_CHANGED" };
		constexpr const char* DATA_CHANNEL_OPEN{ "DATA_CHANNEL_OPEN" };
		constexpr const char* DATA_CHANNEL_CLOSED{ "DATA_CHANNEL_CLOSED" };
		constexpr const char* DATA_CHANNEL_ERROR{ "DATA_CHANNEL_ERROR" };
		constexpr const char* NEGOTIATION_NEEDED{ "NEGOTIATION_NEEDED" };
		constexpr const char* SIGNALING_STATE_CHANGE{ "SIGNALING_STATE_CHANGE" };
		constexpr const char* ICE_CANDIDATE{ "ICE_CANDIDATE" };
		constexpr const char* ICE_CANDIDATE_ERROR{ "ICE_CANDIDATE_ERROR" };
		constexpr const char* PEER_CONNECTION_ICE_PATH_CHANGED{ "PEER_CONNECTION_ICE_PATH_CHANGED" };
		constexpr const char* ICE_RESTART{ "ICE_RESTART" };
		constexpr const char* ICE_RESTART_RECOMMENDED{ "ICE_RESTART_RECOMMENDED" };
		constexpr const char* LONG_PC_CONNECTION_ESTABLISHMENT{ "LONG_PC_CONNECTION_ESTABLISHMENT" };
		constexpr const char* EXCESSIVE_SYNTHESIZED_AUDIO{ "EXCESSIVE_SYNTHESIZED_AUDIO" };
		constexpr const char* CODEC_CHANGED{ "CODEC_CHANGED" };
		constexpr const char* VIDEO_RESOLUTION_CHANGED{ "VIDEO_RESOLUTION_CHANGED" };
		constexpr const char* SIMULCAST_LAYER_CHANGED{ "SIMULCAST_LAYER_CHANGED" };
		constexpr const char* CAPTURE_SOURCE_LOST{ "CAPTURE_SOURCE_LOST" };
		constexpr const char* CAPTURE_TRACK_MUTED{ "CAPTURE_TRACK_MUTED" };
		constexpr const char* STATS_COLLECTION_GAP{ "STATS_COLLECTION_GAP" };
		constexpr const char* TAB_VISIBILITY_CHANGED{ "TAB_VISIBILITY_CHANGED" };
		constexpr const char* PRODUCER_ADDED{ "PRODUCER_ADDED" };
		constexpr const char* PRODUCER_REMOVED{ "PRODUCER_REMOVED" };
		constexpr const char* PRODUCER_PAUSED{ "PRODUCER_PAUSED" };
		constexpr const char* PRODUCER_RESUMED{ "PRODUCER_RESUMED" };
		constexpr const char* CONSUMER_ADDED{ "CONSUMER_ADDED" };
		constexpr const char* CONSUMER_REMOVED{ "CONSUMER_REMOVED" };
		constexpr const char* CONSUMER_PAUSED{ "CONSUMER_PAUSED" };
		constexpr const char* CONSUMER_RESUMED{ "CONSUMER_RESUMED" };
		constexpr const char* DATA_PRODUCER_CREATED{ "DATA_PRODUCER_CREATED" };
		constexpr const char* DATA_PRODUCER_CLOSED{ "DATA_PRODUCER_CLOSED" };
		constexpr const char* DATA_CONSUMER_CREATED{ "DATA_CONSUMER_CREATED" };
		constexpr const char* DATA_CONSUMER_CLOSED{ "DATA_CONSUMER_CLOSED" };
	} // namespace ClientEventTypes

	/** The client meta data types. Port of the JS `ClientMetaTypes`. */
	namespace ClientMetaTypes
	{
		constexpr const char* MEDIA_CONSTRAINT{ "MEDIA_CONSTRAINT" };
		constexpr const char* MEDIA_DEVICE{ "MEDIA_DEVICE" };
		constexpr const char* MEDIA_DEVICES_SUPPORTED_CONSTRAINTS{ "MEDIA_DEVICES_SUPPORTED_CONSTRAINTS" };
		constexpr const char* USER_MEDIA_ERROR{ "USER_MEDIA_ERROR" };
		constexpr const char* LOCAL_SDP{ "LOCAL_SDP" };
		constexpr const char* REMOTE_SDP{ "REMOTE_SDP" };
		constexpr const char* AUDIO_INPUT_DEVICE{ "AUDIO_INPUT_DEVICE" };
		constexpr const char* OPERATION_SYSTEM{ "OPERATION_SYSTEM" };
		constexpr const char* ENGINE{ "ENGINE" };
		constexpr const char* PLATFORM{ "PLATFORM" };
		constexpr const char* BROWSER{ "BROWSER" };
	} // namespace ClientMetaTypes
} // namespace observertc::clientmonitor

#endif
