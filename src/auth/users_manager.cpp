#include "../types.hpp"
#include "users.hpp"
#include "users_manager.hpp"

struct UserSlot {
	
	users::User currentUser;
	bool isActive;
	bool isRetired;
	uint16_t generation;
};

namespace users {

	static UserSlot usersTable[MAX_USERS]{};
	
	static uint16_t nextUserId = 1;
	
	// you might see some repeated lines, dw about it, gonna write some inline funcs soon // TheyCallMeEzraieel
	bool createUser(const char* userName, const char* hashPassword) {
		
		if (nextUserId == 0 || nextUserId == 0xFFFF)
			return false;
		if (userName == nullptr || hashPassword == nullptr)
			return false;
		if (userName[0] == '\0' || hashPassword[0] == '\0')
			return false;
		if (strnlen(userName, sizeof(users::User{}.userName)) >= sizeof(users::User{}.userName))
			return false;
		if (strnlen(hashPassword, sizeof(users::User{}.hashPassword)) >= sizeof(users::User{}.hashPassword))
			return false;
		uint16_t freeSlot = INVALID_SLOT;
		for (uint16_t index=0; index < MAX_USERS; ++index) {
			if (usersTable[index].isActive) {
				if (strcmp(usersTable[index].currentUser.userName, userName) == 0)
					return false;
			}
			else if (!usersTable[index].isRetired && freeSlot == INVALID_SLOT) {
				freeSlot = index;
			}
		}
		if (freeSlot == INVALID_SLOT)
			return false;
		users::User user{};
		user.userId = nextUserId;
		if (strcpy_s(user.userName, userName, sizeof(user.userName)) == nullptr)
			return false;
		if (strcpy_s(user.hashPassword, hashPassword, sizeof(user.hashPassword)) == nullptr)
			return false;
		user.role = users::Roles::USER;
		usersTable[freeSlot].currentUser = user;
		usersTable[freeSlot].isActive = true;
		nextUserId++;
		return true;
	}
	
	bool dropUser(UserHandle handle) {
		if (handle.slot >= MAX_USERS)
			return false;
		if (!usersTable[handle.slot].isActive)
			return false;
		if (usersTable[handle.slot].generation != handle.generation)
			return false;
		usersTable[handle.slot].currentUser = {};
		usersTable[handle.slot].isActive = false;
		if (usersTable[handle.slot].generation < 0xFFFF) // I'd know this one is a lil bit messy but I'll figure it out later @ org schema // TheyCallMeEzraieel
			usersTable[handle.slot].generation++;
		else
			usersTable[handle.slot].isRetired = true;
		return true;
	}
	
	UserHandle getUserHandleById(uint16_t userId) {
		UserHandle invalidHandle{};
		invalidHandle.slot = INVALID_SLOT;
		for (uint16_t index=0; index < MAX_USERS; ++index) {
			if (!usersTable[index].isActive)
				continue;
			if (usersTable[index].currentUser.userId == userId) {
				UserHandle uHandle;
				uHandle.slot = index;
				uHandle.generation = usersTable[index].generation;
				return uHandle;
			}
		}
		return invalidHandle;
	}
	
	UserHandle getUserHandleByName(const char* userName) {
		UserHandle invalidHandle{};
		invalidHandle.slot = INVALID_SLOT;
		if (userName == nullptr || userName[0] == '\0')
			return invalidHandle;
		for (uint16_t index=0; index < MAX_USERS; ++index) {
			if (!usersTable[index].isActive)
				continue;
			if (strcmp(usersTable[index].currentUser.userName, userName) == 0) {
				UserHandle uHandle;
				uHandle.slot = index;
				uHandle.generation = usersTable[index].generation;
				return uHandle;
			}
		}
		return invalidHandle;
	}
	
	bool pullUser(UserHandle handle, User& output) {
		if (handle.slot >= MAX_USERS)
			return false;
		if (!usersTable[handle.slot].isActive)
			return false;
		if (usersTable[handle.slot].generation != handle.generation)
			return false;
		output = usersTable[handle.slot].currentUser;
		return true;
	}
	
	bool setUserRole(UserHandle handle, Roles role) {
		if (handle.slot >= MAX_USERS)
			return false;
		if (!usersTable[handle.slot].isActive)
			return false;
		if (handle.generation != usersTable[handle.slot].generation)
			return false;
		if (role != Roles::USER &&
			role != Roles::ADMIN &&
			role != Roles::SUPERADMIN)
			return false;
		usersTable[handle.slot].currentUser.role = role;
		return true;
	}
	
	bool setUserName(UserHandle handle, const char* userName) {
		if (handle.slot >= MAX_USERS)
			return false;
		if (userName == nullptr)
			return false;
		if (strnlen(userName, sizeof(usersTable[handle.slot].currentUser.userName)) >= sizeof(usersTable[handle.slot].currentUser.userName))
			return false;
		if (userName[0] == '\0')
			return false;
		if (!usersTable[handle.slot].isActive)
			return false;
		if (handle.generation != usersTable[handle.slot].generation)
			return false;
		for (uint16_t index=0; index < MAX_USERS; ++index) {
			if (index == handle.slot || !usersTable[index].isActive)
				continue;
			if (strcmp(userName, usersTable[index].currentUser.userName) == 0)
				return false;
		}
		if (strcpy_s(usersTable[handle.slot].currentUser.userName, userName, sizeof(usersTable[handle.slot].currentUser.userName)) == nullptr)
			return false;
		return true;
	}
	
	bool setUserHashPassword(UserHandle handle, const char* hashPassword) {
		if (handle.slot >= MAX_USERS)
			return false;
		if (hashPassword == nullptr)
			return false;
		if (hashPassword[0] == '\0')
			return false;
		if (strnlen(hashPassword, sizeof(usersTable[handle.slot].currentUser.hashPassword)) >= sizeof(usersTable[handle.slot].currentUser.hashPassword))
			return false;
		if (!usersTable[handle.slot].isActive)
			return false;
		if (handle.generation != usersTable[handle.slot].generation)
			return false;
		if (strcpy_s(usersTable[handle.slot].currentUser.hashPassword, hashPassword, sizeof(usersTable[handle.slot].currentUser.hashPassword)) == nullptr)
			return false;
		return true;
	}
}
