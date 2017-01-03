#include "user.hpp"

User::User(const std::string &u, const std::string &p) 
{
	username = u;
	password = p;
}

User::User(const std::string &u, const std::string &h, const std::string &p)
{
	username = u;
	hostname = h;
	port = p;
}

void User::addFriend(const std::string &uname)
{
	friend_list.push_back(uname);
}

void User::setAddressInfo(const std::string &h, const std::string &p)
{
	hostname = h;
	port = p;
}

bool User::hasFriend(const std::string &uname) const
{
	for (unsigned i = 0; i < friend_list.size(); ++i) {
		if (friend_list[i] == uname) {
			return true;
		}
	}
	return false;
}

std::string User::infoToString() const
{
	std::string info = username + "|" + password + "|";
	int num_contacts = friend_list.size();
	if (num_contacts == 1) {
		info += friend_list[0];
	} else if (num_contacts > 1) {
		for (int i = 0; i < num_contacts - 1; ++i) {
			info += friend_list[i];
			info += ';';
		}
		info += friend_list.at(num_contacts - 1);
	}
	return info;
}

std::string User::getUsername() const
{
	return username;
}

std::string User::getPassword() const
{
	return password;
}

std::string User::getHostname() const
{
	return hostname;
}

std::string User::getPort() const
{
	return port;
}