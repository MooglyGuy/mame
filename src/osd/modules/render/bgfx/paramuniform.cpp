// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  paramuniform.cpp - BGFX shader effect parametric uniform
//
//============================================================

#include "paramuniform.h"

#include "parameter.h"

bgfx_param_uniform::bgfx_param_uniform(bgfx_uniform* uniform, bgfx_parameter* param)
	: bgfx_entry_uniform(uniform)
	, m_param(param)
{
}

bgfx_entry_uniform* bgfx_param_uniform::from_json(const Value& value, const std::string &prefix, bgfx_uniform* uniform, std::map<std::string, bgfx_parameter*>& params)
{
	if (!validate_parameters(value, prefix))
	{
		return nullptr;
	}

	std::string parameter = value["parameter"].GetString();

	return new bgfx_param_uniform(uniform, params[parameter]);
}

void bgfx_param_uniform::bind()
{
	float value = m_param->value();
	m_uniform->set(&value, sizeof(float));
}

bool bgfx_param_uniform::validate_parameters(const Value& value, const std::string &prefix)
{
	if (!READER_CHECK(value.HasMember("parameter"), "%sMust have string value 'parameter' (what parameter is being mapped?)\n", prefix)) return false;
	if (!READER_CHECK(value["parameter"].IsString(), "%sValue 'parameter' must be a string\n", prefix)) return false;
	return true;
}
