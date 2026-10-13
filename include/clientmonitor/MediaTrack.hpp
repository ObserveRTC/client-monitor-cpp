#ifndef CM_MEDIA_TRACK_HPP
#define CM_MEDIA_TRACK_HPP

#include <optional>
#include <string>

namespace clientmonitor
{
	/**
	 * What the monitor needs to know about a media track. The JS library reads a browser
	 * `MediaStreamTrack`; natively the application (or the libwebrtc binding) supplies one of
	 * these, so the core does not depend on libwebrtc.
	 *
	 * Implementations must be safe to call from the collecting thread.
	 */
	class MediaTrack
	{
	public:
		enum class ReadyState
		{
			LIVE,
			ENDED
		};

		/** The subset of `MediaTrackSettings` the detectors read. */
		struct Settings
		{
			std::optional<std::string> deviceId;
			std::optional<std::string> groupId;
			std::optional<std::string> displaySurface;
			std::optional<double> width;
			std::optional<double> height;
			std::optional<double> frameRate;
		};

	public:
		virtual ~MediaTrack() = default;

		virtual std::string GetId() const = 0;
		// "audio" or "video".
		virtual std::string GetKind() const      = 0;
		virtual ReadyState GetReadyState() const = 0;
		virtual std::string GetLabel() const
		{
			return "";
		}
		virtual bool IsMuted() const
		{
			return false;
		}
		virtual bool IsEnabled() const
		{
			return true;
		}
		virtual std::optional<Settings> GetSettings() const
		{
			return std::nullopt;
		}
	};

	/**
	 * A MediaTrack whose state the application sets by hand. Useful when the track lives in a
	 * stack the monitor has no binding for, and in tests.
	 */
	class BasicMediaTrack : public MediaTrack
	{
	public:
		BasicMediaTrack(std::string id, std::string kind, std::string label = "")
		  : id(std::move(id)), kind(std::move(kind)), label(std::move(label))
		{
		}

	public:
		std::string GetId() const override
		{
			return this->id;
		}
		std::string GetKind() const override
		{
			return this->kind;
		}
		ReadyState GetReadyState() const override
		{
			return this->readyState;
		}
		std::string GetLabel() const override
		{
			return this->label;
		}
		bool IsMuted() const override
		{
			return this->muted;
		}
		bool IsEnabled() const override
		{
			return this->enabled;
		}
		std::optional<Settings> GetSettings() const override
		{
			return this->settings;
		}

		void SetReadyState(ReadyState state)
		{
			this->readyState = state;
		}
		void SetMuted(bool muted)
		{
			this->muted = muted;
		}
		void SetEnabled(bool enabled)
		{
			this->enabled = enabled;
		}
		void SetSettings(std::optional<Settings> settings)
		{
			this->settings = std::move(settings);
		}

	private:
		std::string id;
		std::string kind;
		std::string label;
		ReadyState readyState{ ReadyState::LIVE };
		bool muted{ false };
		bool enabled{ true };
		std::optional<Settings> settings;
	};
} // namespace clientmonitor

#endif
