#include <string>
#include <unordered_set>
#include <iostream>

class IgnoreList
{
public:
	explicit IgnoreList(bool invert = false)
		: invert_(invert) {}

	void add(const std::string &item)
	{
		items_.insert(item);
	}

	void remove(const std::string &item)
	{
		items_.erase(item);
	}

	void clear()
	{
		items_.clear();
	}

	void setInvert(bool invert)
	{
		invert_ = invert;
	}

	bool match(const std::string &item) const
	{
		bool found = (items_.count(item) > 0);
		return invert_ ? !found : found;
	}

private:
	std::unordered_set<std::string> items_;
	bool invert_;
};

