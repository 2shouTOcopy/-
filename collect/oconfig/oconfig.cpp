#include <cerrno>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "oconfig.h"

using namespace std;

unique_ptr<ConfigParser> ConfigParser::create()
{
	return std::unique_ptr<ConfigParser>(new ConfigParser());
}

string ConfigParser::trim(const string& s)
{
	size_t start = 0;
	while (start < s.size() && isspace(static_cast<unsigned char>(s[start]))) 
		++start;
	if (start == s.size()) return "";

	size_t end = s.size() - 1;
	while (end > start && isspace(static_cast<unsigned char>(s[end])))
		--end;
	return s.substr(start, end - start + 1);
}

string ConfigParser::stripQuotes(const string& s)
{
	if (s.size() >= 2 && 
		((s.front() == '"' && s.back() == '"') || 
		(s.front() == '\'' && s.back() == '\'')))
	{
		return s.substr(1, s.size() - 2);
	}
	return s;
}

vector<string> ConfigParser::tokenizeLine(const string& line)
{
	vector<string> tokens;
	bool inQuotes = false;
	char quoteChar = 0;
	ostringstream current;

	for (char c : line)
	{
		if (!inQuotes)
		{
			if ((c == '"' || c == '\'') && !isspace(c))
			{
				inQuotes = true;
				quoteChar = c;
				current << c;
			}
			else if (isspace(c))
			{
				if (current.tellp() > 0)
				{
					tokens.push_back(current.str());
					current.str("");
					current.clear();
				}
			}
			else
			{
				current << c;
			}
		}
		else
		{
			current << c;
			if (c == quoteChar)
			{
				inQuotes = false;
			}
		}
	}

	if (current.tellp() > 0)
	{
		tokens.push_back(current.str());
	}

	for (auto& t : tokens)
	{
		t = stripQuotes(trim(t));
	}
	return tokens;
}

int ConfigParser::parseBlock(FILE* fh, OConfigItem& parent, int& lineNum)
{
	char buf[1024];
	while (fgets(buf, sizeof(buf), fh))
	{
		++lineNum;
		string line = trim(buf);

		if (line.empty() || line[0] == '#') continue;

		// 处理块结束
		if (line.size() >= 2 && line[0] == '<' && line[1] == '/')
		{
			return 0;
		}

		// 处理块开始
		if (line[0] == '<' && line[1] != '/')
		{
			size_t endPos = line.find('>');
			if (endPos == string::npos) endPos = line.size();

			string content = trim(line.substr(1, endPos - 1));
			auto tokens = tokenizeLine(content);
			if (tokens.empty())
			{
				throw runtime_error("Empty block at line " + to_string(lineNum));
			}

			auto* child = parent.addChild(tokens[0]);
			for (size_t i = 1; i < tokens.size(); ++i)
			{
				child->addValue(OConfigValue{tokens[i]});
			}

			int status = parseBlock(fh, *child, lineNum);
			if (status != 0) return status;
			continue;
		}

		// 处理普通行
		auto tokens = tokenizeLine(line);
		if (!tokens.empty())
		{
			auto* child = parent.addChild(tokens[0]);
			for (size_t i = 1; i < tokens.size(); ++i)
			{
				child->addValue(OConfigValue{tokens[i]});
			}
		}
	}
	return 0;
}

int ConfigParser::parseStream(FILE* fh, OConfigItem& root)
{
	int lineNum = 0;
	try
	{
		return parseBlock(fh, root, lineNum);
	}
	catch (const exception& e)
	{
		cerr << "Parse error: " << e.what() << endl;
		return -1;
	}
}

unique_ptr<OConfigItem> ConfigParser::parseFile(const char* filename)
{
	auto root = make_unique<OConfigItem>("root");
	FILE* fh = fopen(filename, "r");
	if (!fh)
	{
		throw runtime_error("Cannot open file: " + string(filename) + 
		                  " (" + strerror(errno) + ")");
	}

	int status = parseStream(fh, *root);
	fclose(fh);

	if (status != 0 || root->children.empty())
	{
		return nullptr;
	}
	return root;
}

unique_ptr<OConfigItem> ConfigParser::parseString(const char* buffer)
{
	auto root = make_unique<OConfigItem>("root");
	FILE* fh = tmpfile();
	if (!fh)
	{
		throw runtime_error("Create temp file failed");
	}

	fputs(buffer, fh);
	rewind(fh);

	int status = parseStream(fh, *root);
	fclose(fh);

	return (status == 0) ? move(root) : nullptr;
}

