#pragma once
#include <string>
#include <vector>
#include "PropertyType.hpp"

class ResStruct {
public:
	std::string m_name;
	std::vector<PropertyType> m_properties;
};