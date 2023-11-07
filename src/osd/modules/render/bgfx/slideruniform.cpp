// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  slideruniform.cpp - BGFX shader effect slider uniform
//
//  Represents the mapping between a slider and an effect
//  shader uniform for a given entry
//
//============================================================

#include "slideruniform.h"

#include "slider.h"

#include <utility>

bgfx_slider_uniform::bgfx_slider_uniform(bgfx_uniform* uniform, std::vector<bgfx_slider*> &&sliders)
	: bgfx_entry_uniform(uniform)
	, m_sliders(std::move(sliders))
{
}

bgfx_entry_uniform* bgfx_slider_uniform::from_json(const Value& value, const std::string &prefix, bgfx_uniform* uniform, std::map<std::string, bgfx_slider*>& sliders)
{
	if (!validate_parameters(value, prefix))
	{
		return nullptr;
	}

	std::string name = value["slider"].GetString();
	std::vector<bgfx_slider*> slider_list;
	slider_list.push_back(sliders[name + "0"]);

	if (slider_list[0]->type() == bgfx_slider::slider_type::SLIDER_VEC2)
	{
		slider_list.push_back(sliders[name + "1"]);
	}
	else if (slider_list[0]->type() == bgfx_slider::slider_type::SLIDER_COLOR)
	{
		slider_list.push_back(sliders[name + "1"]);
		slider_list.push_back(sliders[name + "2"]);
	}

	return new bgfx_slider_uniform(uniform, std::move(slider_list));
}

void bgfx_slider_uniform::bind()
{
	float values[4];
	for (uint32_t i = 0; i < m_sliders.size(); i++)
	{
		values[i] = m_sliders[i]->uniform_value();
	}
	m_uniform->set(values, sizeof(float) * m_sliders.size());
}

bool bgfx_slider_uniform::validate_parameters(const Value& value, const std::string &prefix)
{
	if (!READER_CHECK(value.HasMember("slider"), "%sMust have string value 'slider' (what slider are we getting the value of?)\n", prefix)) return false;
	if (!READER_CHECK(value["slider"].IsString(), "%sValue 'slider' must be a string\n", prefix)) return false;
	return true;
}
