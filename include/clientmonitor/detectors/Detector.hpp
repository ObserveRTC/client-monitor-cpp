#ifndef CM_DETECTORS_DETECTOR_HPP
#define CM_DETECTORS_DETECTOR_HPP

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace observertc::clientmonitor
{
	/**
	 * A detector reaches one verdict about one subject (the client, a peer connection, a track,
	 * an ICE transport) on every collection. Port of the JS `Detector` interface.
	 */
	class Detector
	{
	public:
		virtual ~Detector() = default;

		virtual const std::string& GetName() const = 0;
		virtual void Update()                      = 0;

	public:
		bool IsDisabled() const
		{
			return this->disabled;
		}
		void SetDisabled(bool disabled)
		{
			this->disabled = disabled;
		}
		bool GetIncludeIssueInSample() const
		{
			return this->includeIssueInSample;
		}
		void SetIncludeIssueInSample(bool include)
		{
			this->includeIssueInSample = include;
		}

	protected:
		bool disabled{ false };
		bool includeIssueInSample{ true };
	};

	/**
	 * The detectors attached to one subject, run in registration order. Adding a detector under a
	 * name already taken replaces it in place.
	 */
	class Detectors
	{
	public:
		Detectors()                            = default;
		Detectors(const Detectors&)            = delete;
		Detectors& operator=(const Detectors&) = delete;

	public:
		/** Takes ownership. Returns the raw pointer for convenience. */
		Detector* Add(std::unique_ptr<Detector> detector);
		bool Remove(const std::string& name);
		void Clear();
		size_t GetSize() const;
		std::vector<std::string> GetNames() const;
		bool Has(const std::string& name) const;
		Detector* GetByName(const std::string& name) const;
		bool Disable(const std::string& name);
		bool Enable(const std::string& name);
		void DisableAll();
		void EnableAll();
		bool IsEnabled(const std::string& name) const;
		void ForEach(const std::function<void(Detector*)>& callback) const;
		/** Runs every enabled detector. A detector that throws is logged and skipped. */
		void Update();

	private:
		std::vector<std::unique_ptr<Detector>> detectors;
	};
} // namespace observertc::clientmonitor

#endif
