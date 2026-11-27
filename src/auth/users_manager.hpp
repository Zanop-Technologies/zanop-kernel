#pragma once

namespace users {
	
	constexpr uint16_t MAX_USERS = 32;
	
	constexpr uint16_t INVALID_SLOT = 0xFFFF;
	
	UserHandle getUserHandleById(uint16_t userId);
	
	UserHandle getUserHandleByName(const char* userName);
	
	bool pullUser(UserHandle handle, User& output);
	
	bool createUser(const char* userName, const char* hashPassword);
	
	bool dropUser(uint16_t userId);
	
	bool setUserRole(uint16_t userId, Roles role);
	
	bool setUserName(uint16_t userId, const char* userName);
	
	bool setUserHashPassword(uint16_t userId, const char* hashPassword);
	
}