// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  valueuniform.cpp - BGFX shader effect fixed uniform
//
//  Represents the mapping between a fixed value and an effect
//  shader uniform for a given entry
//
//============================================================

#include "valueuniform.h"

#include <algorithm>
#include <cassert>


bgfx_value_uniform::bgfx_value_uniform(bgfx_uniform* uniform, const float* values, const int count)
	: bgfx_entry_uniform(uniform)
	, m_count(count)
{
	assert(count <= std::size(m_values));
	std::copy_n(values, count, m_values);
}

bgfx_entry_uniform* bgfx_value_uniform::from_json(const Value& value, const std::string &prefix, bgfx_uniform* uniform)
{
	if (!validate_parameters(value, prefix))
	{
		return nullptr;
	}

	float values[4];
	int count = 1;
	if (value["value"].IsNumber())
	{
		values[0] = float(value["value"].GetDouble());
	}
	else
	{
		const Value& value_array = value["value"];
		count = int(value_array.Size());
		for (int i = 0; i < count; i++)
		{
			values[i] = float(value_array[i].GetDouble());
		}
	}
	return new bgfx_value_uniform(uniform, values, count);
}

void bgfx_value_uniform::bind()
{
	m_uniform->set(m_values, sizeof(float) * m_count);
}

bool bgfx_value_uniform::validate_parameters(const Value& value, const std::string &prefix)
{
	if (!READER_CHECK(value.HasMember("value"), "%sMust have string value 'value' (what value is being assigned?)\n", prefix)) return false;
	if (!READER_CHECK(value["value"].IsArray() || value["value"].IsNumber(), "%sValue 'value' must be numeric or an array\n", prefix)) return false;
	return true;
}
