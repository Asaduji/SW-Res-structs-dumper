#pragma once
#include <Windows.h>
#include <string>
#include <sstream>
#include <vector>
#include "psapi.h"

uintptr_t find_pattern(HMODULE module, std::string pattern) {

	if (module == NULL) {
		return NULL;
	}

	MODULEINFO module_info;
	if (!GetModuleInformation(GetCurrentProcess(), module, &module_info, sizeof(MODULEINFO))) {
		return NULL;
	}
	auto* module_base = static_cast<const char*>(module_info.lpBaseOfDll);
	auto module_size = module_info.SizeOfImage;

	//convert bytes string to pattern bytes and mask bytes
	std::vector<char> pattern_bytes;
	std::vector<char> mask;
	std::stringstream ss(pattern);
	std::string byte_str;
	while (ss >> byte_str) {
		if (byte_str == "?") {
			pattern_bytes.push_back(0x00);

			//comparing to bool is way slower so we just use char instead
			mask.push_back('?');
		}
		else {
			pattern_bytes.push_back(static_cast<char>(std::stoi(byte_str, nullptr, 16)));
			mask.push_back(' ');
		}
	}

	auto pattern_size = pattern_bytes.size();
	auto max_offset = module_size - pattern_size;

	for (size_t offset = 0; offset <= max_offset; ++offset) {
		auto* module_bytes = module_base + offset;

		//search memory
		for (size_t i = 0; i < pattern_size; ++i) {
			if (mask[i] != '?' && module_bytes[i] != pattern_bytes[i]) {
				break;
			}
			//if this is the last byte in the pattern, we found it
			else if (i == pattern_size - 1) {
				return reinterpret_cast<uintptr_t>(module_bytes);
			}
		}
	}

	return NULL;
}