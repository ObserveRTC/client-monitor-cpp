#define CM_CLASS "Detectors"

#include "clientmonitor/Logger.hpp"
#include "clientmonitor/detectors/Detector.hpp"

#include <algorithm>
#include <exception>

namespace clientmonitor
{
	Detector* Detectors::Add(std::unique_ptr<Detector> detector)
	{
		if (!detector)
		{
			return nullptr;
		}

		auto* raw = detector.get();

		for (auto& existing : this->detectors)
		{
			if (existing->GetName() == raw->GetName())
			{
				existing = std::move(detector);

				return raw;
			}
		}

		this->detectors.push_back(std::move(detector));

		return raw;
	}

	bool Detectors::Remove(const std::string& name)
	{
		const auto size = this->detectors.size();

		this->detectors.erase(
		  std::remove_if(
		    this->detectors.begin(),
		    this->detectors.end(),
		    [&name](const std::unique_ptr<Detector>& detector)
		    {
			    return detector->GetName() == name;
		    }),
		  this->detectors.end());

		return size != this->detectors.size();
	}

	void Detectors::Clear()
	{
		this->detectors.clear();
	}

	size_t Detectors::GetSize() const
	{
		return this->detectors.size();
	}

	std::vector<std::string> Detectors::GetNames() const
	{
		std::vector<std::string> names;

		for (const auto& detector : this->detectors)
		{
			names.push_back(detector->GetName());
		}

		return names;
	}

	bool Detectors::Has(const std::string& name) const
	{
		return this->GetByName(name) != nullptr;
	}

	Detector* Detectors::GetByName(const std::string& name) const
	{
		for (const auto& detector : this->detectors)
		{
			if (detector->GetName() == name)
			{
				return detector.get();
			}
		}

		return nullptr;
	}

	bool Detectors::Disable(const std::string& name)
	{
		auto* detector = this->GetByName(name);

		if (!detector)
		{
			return false;
		}

		detector->SetDisabled(true);

		return true;
	}

	bool Detectors::Enable(const std::string& name)
	{
		auto* detector = this->GetByName(name);

		if (!detector)
		{
			return false;
		}

		detector->SetDisabled(false);

		return true;
	}

	void Detectors::DisableAll()
	{
		for (auto& detector : this->detectors)
		{
			detector->SetDisabled(true);
		}
	}

	void Detectors::EnableAll()
	{
		for (auto& detector : this->detectors)
		{
			detector->SetDisabled(false);
		}
	}

	bool Detectors::IsEnabled(const std::string& name) const
	{
		auto* detector = this->GetByName(name);

		return detector && !detector->IsDisabled();
	}

	void Detectors::ForEach(const std::function<void(Detector*)>& callback) const
	{
		for (const auto& detector : this->detectors)
		{
			callback(detector.get());
		}
	}

	void Detectors::Update()
	{
		for (size_t i = 0; i < this->detectors.size(); ++i)
		{
			auto* detector = this->detectors[i].get();

			if (detector->IsDisabled())
			{
				continue;
			}

			try
			{
				detector->Update();
			}
			catch (const std::exception& error)
			{
				CM_WARN("error updating detector %s: %s", detector->GetName().c_str(), error.what());
			}
		}
	}
} // namespace clientmonitor
