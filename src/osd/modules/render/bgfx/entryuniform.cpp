// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  entryuniform.cpp - BGFX shader effect uniform remapper
//
//  Represents the mapping between a fixed value, a slider, or
//  other dynamic parameter and an effect shader uniform
//
//============================================================

#include "entryuniform.h"

#include "paramuniform.h"
#include "shader.h"
#include "slideruniform.h"
#include "uniform.h"
#include "valueuniform.h"

bgfx_entry_uniform* bgfx_entry_uniform::from_json(const Value& value, const std::string &prefix, bgfx_shader* shader, std::map<std::string, bgfx_slider*>& sliders, std::map<std::string, bgfx_parameter*>& params)
{
	if (!validate_parameters(value, prefix))
	{
		return nullptr;
	}

	std::string name = value["uniform"].GetString();
	bgfx_uniform* uniform = shader->uniform(name);

	if (!READER_CHECK(uniform != nullptr, "%sUniform '%s' does not appear to exist\n", prefix, name))
	{
		return nullptr;
	}

	if (value.HasMember("slider"))
	{
		return bgfx_slider_uniform::from_json(value, prefix, uniform, sliders);
	}
	else if (value.HasMember("value"))
	{
		return bgfx_value_uniform::from_json(value, prefix, uniform);
	}
	else if (value.HasMember("parameter"))
	{
		return bgfx_param_uniform::from_json(value, prefix, uniform, params);
	}
	else
	{
		READER_CHECK(false, "%sUnrecognized uniform type for uniform binding %s", prefix, name);
	}


	return nullptr;
}

bool bgfx_entry_uniform::validate_parameters(const Value& value, const std::string &prefix)
{
	if (!READER_CHECK(value.HasMember("uniform"), "%sMust have string value 'uniform' (what uniform are we mapping?)\n", prefix)) return false;
	if (!READER_CHECK(value["uniform"].IsString(), "%sValue 'uniform' must be a string\n", prefix)) return false;
	return true;
}
