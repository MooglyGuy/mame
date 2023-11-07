// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  suppressor.cpp - Conditionally suppress a bgfx effect entry
//  from being processed.
//
//============================================================

#include "suppressor.h"

#include "slider.h"

#include <cstring>


const bgfx_suppressor::string_to_enum bgfx_suppressor::CONDITION_NAMES[bgfx_suppressor::CONDITION_COUNT] = {
	{ "equal",      bgfx_suppressor::condition_type::CONDITION_EQUAL },
	{ "notequal",   bgfx_suppressor::condition_type::CONDITION_NOTEQUAL }
};

const bgfx_suppressor::string_to_enum bgfx_suppressor::COMBINE_NAMES[bgfx_suppressor::COMBINE_COUNT] = {
	{ "and", bgfx_suppressor::combine_mode::COMBINE_AND },
	{ "or",  bgfx_suppressor::combine_mode::COMBINE_OR }
};


bgfx_suppressor::bgfx_suppressor(std::vector<bgfx_slider*> &&sliders, uint32_t condition, combine_mode combine, void* value)
	: m_sliders(std::move(sliders))
	, m_condition(condition)
	, m_combine(combine)
	, m_value(nullptr)
{
	uint32_t size = m_sliders[0]->size();
	m_value = new uint8_t[size];
	memcpy(m_value, value, size);
}

bgfx_suppressor::~bgfx_suppressor()
{
	delete [] m_value;
}

bgfx_suppressor* bgfx_suppressor::from_json(const Value& value, const std::string &prefix, std::map<std::string, bgfx_slider*>& sliders)
{
	if (!validate_parameters(value, prefix))
	{
		return nullptr;
	}

	std::string name = value["name"].GetString();
	uint32_t condition = uint32_t(get_enum_from_value(value, "condition", bgfx_suppressor::condition_type::CONDITION_EQUAL, CONDITION_NAMES, CONDITION_COUNT));
	bgfx_suppressor::combine_mode mode = bgfx_suppressor::combine_mode(get_enum_from_value(value, "combine", bgfx_suppressor::combine_mode::COMBINE_OR, COMBINE_NAMES, COMBINE_COUNT));

	std::vector<bgfx_slider*> check_sliders;
	check_sliders.push_back(sliders[name + "0"]);

	int slider_count;
	switch (check_sliders[0]->type())
	{
		case bgfx_slider::slider_type::SLIDER_FLOAT:
		case bgfx_slider::slider_type::SLIDER_INT:
		case bgfx_slider::slider_type::SLIDER_INT_ENUM:
			slider_count = 1;
			break;
		case bgfx_slider::slider_type::SLIDER_VEC2:
			slider_count = 2;
			break;
		case bgfx_slider::slider_type::SLIDER_COLOR:
			slider_count = 3;
			break;
		default:
			slider_count = 0;
			break;
	}

	int values[4];
	if (slider_count > 1)
	{
		get_values(value, prefix, "value", values, slider_count);
		if (!READER_CHECK(slider_count == value["value"].GetArray().Size(), "%sExpected %d values, got %u\n", prefix, slider_count, value["value"].GetArray().Size())) return nullptr;
		for (int index = 1; index < slider_count; index++)
		{
			check_sliders.push_back(sliders[name + std::to_string(index)]);
		}
	}
	else
	{
		values[0] = get_int(value, "value", 0);
	}

	return new bgfx_suppressor(std::move(check_sliders), condition, mode, values);
}

bool bgfx_suppressor::suppress()
{
	int32_t count = 1;
	if (m_sliders[0]->type() == bgfx_slider::slider_type::SLIDER_VEC2)
	{
		count = 2;
	}
	else if (m_sliders[0]->type() == bgfx_slider::slider_type::SLIDER_COLOR)
	{
		count = 3;
	}

	float current_values[3];
	for (int32_t index = 0; index < count; index++)
	{
		current_values[index] = m_sliders[index]->value();
	}

	switch (m_condition)
	{
		case CONDITION_NOTEQUAL:
			return memcmp(m_value, current_values, m_sliders[0]->size()) != 0;
		case CONDITION_EQUAL:
			return memcmp(m_value, current_values, m_sliders[0]->size()) == 0;
		default:
			return false;
	}
}

bool bgfx_suppressor::get_values(const Value& value, std::string prefix, std::string name, int* values, const int count)
{
	const Value& value_array = value[name.c_str()];
	for (uint32_t i = 0; i < value_array.Size() && i < count; i++)
	{
		if (!READER_CHECK(value_array[i].IsInt(), "%svalue[%u] must be an integer\n", prefix, i)) return false;
		values[i] = value_array[i].GetInt();
	}
	return true;
}

bool bgfx_suppressor::validate_parameters(const Value& value, const std::string &prefix)
{
	if (!READER_CHECK(value["type"].IsString(), "%sValue 'type' must be a string\n", prefix)) return false;
	if (!READER_CHECK(value.HasMember("name"), "%sMust have string value 'name'\n", prefix)) return false;
	if (!READER_CHECK(value["name"].IsString(), "%sValue 'name' must be a string\n", prefix)) return false;
	if (!READER_CHECK(value.HasMember("value"), "%sMust have numeric or array value 'value'\n", prefix)) return false;
	if (!READER_CHECK(value["value"].IsNumber() || value["value"].IsArray(), "%sValue 'value' must be a number or array the size of the corresponding slider type\n", prefix)) return false;
	return true;
}
