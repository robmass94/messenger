#include "utils.hpp"

void trimString(std::string &str)
{
	// left trim
	str.erase(0, str.find_first_not_of(" \t"));
	// right trim
	str.erase(str.find_last_not_of(" \t") + 1);
}

char *createHash(const std::string &str)
{
	return crypt(str.c_str(), "$1$########$");
}