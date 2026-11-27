#pragma once

namespace users {
	
	constexpr uint16_t MAX_USERS = 32;
	
	constexpr uint16_t INVALID_SLOT = 0xFFFF;
	
	UserHandle getUserHandleById(uint16_t userId);
	
	UserHandle getUserHandleByName(const char* userName);
	
	bool pullUser(UserHandle handle, User& output);
	
	bool createUser(const char* userName, const char* hashPassword);
	
	bool dropUser(UserHandle handle);
	
	bool setUserRole(UserHandle handle, Roles role);
	
	bool setUserName(UserHandle handle, const char* userName);
	
	bool setUserHashPassword(UserHandle handle, const char* hashPassword);
	
}