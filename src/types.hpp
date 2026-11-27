#pragma once

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef __SIZE_TYPE__ size_t;
typedef __UINTPTR_TYPE__ uintptr_t;
typedef long double max_align_t;

inline void* memset(void* destination, int value, size_t count) {
	uint8_t* bytes = static_cast<uint8_t*>(destination);
	for (size_t index = 0; index < count; ++index) {
		bytes[index] = static_cast<uint8_t>(value);
	}
	return destination;
}

inline void* memcpy(void* destination, const void* source, size_t count) {
	uint8_t* dest = static_cast<uint8_t*>(destination);
	const uint8_t* src = static_cast<const uint8_t*>(source);
	for (size_t index=0; index<count; ++index) {
		dest[index] = src[index];
	}
	return destination;
}

inline size_t strnlen(const char* string, size_t maxLen) {
	size_t length = 0;
	while (length < maxLen && string[length] != '\0') {
		++length;
	}
	return length;
}

inline size_t strlen(const char* string) {
	size_t length = 0;
	while (string[length] != '\0') {
		++length;
	}
	return length;
}

inline int strcmp(const char* left, const char* right) {
	while (*left && *left == *right) {
		++left;
		++right;
	}
	return static_cast<unsigned char>(*left) - static_cast<unsigned char>(*right);
}

inline char* strncpy(char* destination, const char* source, size_t maxLen) {
	const size_t srcLen = strnlen(source, maxLen);
	memcpy(destination, source, srcLen);
	if (srcLen < maxLen)
		memset(destination + source, 0, maxLen - srcLen);
	return destination;
}

inline char* strcpy(char* destination, const char* source, size_t destinationSize) {
    if (destination == nullptr || source == nullptr || destinationSize == 0)
        return nullptr;
    const size_t length = strnlen(source, destinationSize);
    if (length >= destinationSize)
        return nullptr;
    memcpy(destination, source, length + 1);
    return destination;
}

namespace std {
struct nullopt_t {
	explicit constexpr nullopt_t(int) {}
};

constexpr nullopt_t nullopt{0};

template <typename T>
class optional {
public:
	constexpr optional() : has_value_(false) {}
	constexpr optional(nullopt_t) : has_value_(false) {}
	constexpr optional(const T& value) : has_value_(true), value_(value) {}

	constexpr bool has_value() const { return has_value_; }
	constexpr explicit operator bool() const { return has_value_; }
	constexpr T& operator*() { return value_; }
	constexpr const T& operator*() const { return value_; }
	constexpr T& value() { return value_; }
	constexpr const T& value() const { return value_; }

	template <typename U>
	constexpr T value_or(U&& fallback) const {
		return has_value_ ? value_ : static_cast<T>(fallback);
	}

private:
	bool has_value_;
	T value_;
};

using ::size_t;
using ::uintptr_t;
using ::max_align_t;
using ::uint8_t;
using ::uint16_t;
using ::uint32_t;
using ::uint64_t;
}
