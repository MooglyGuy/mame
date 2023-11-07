// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  parameter.h - BGFX shader parameter
//
//  A value that represents some form of parametric
//  operation, which can be fed to the input of a BGFX
//  shader uniform.
//
//============================================================

#include "parameter.h"

#include "effectmanager.h"
#include "frameparameter.h"
#include "parameter.h"
#include "timeparameter.h"
#include "windowparameter.h"

const bgfx_parameter::string_to_enum bgfx_parameter::TYPE_NAMES[bgfx_parameter::TYPE_COUNT] = {
	{ "frame",  bgfx_parameter::parameter_type::PARAM_FRAME },
	{ "window", bgfx_parameter::parameter_type::PARAM_WINDOW },
	{ "time",   bgfx_parameter::parameter_type::PARAM_TIME }
};

bgfx_parameter* bgfx_parameter::from_json(const Value& value, const std::string &prefix, effect_manager& effects)
{
	if (!validate_parameters(value, prefix))
	{
		return nullptr;
	}

	std::string name = value["name"].GetString();
	parameter_type type = parameter_type(get_enum_from_value(value, "type", parameter_type::PARAM_FRAME, TYPE_NAMES, TYPE_COUNT));

	if (type == parameter_type::PARAM_FRAME)
	{
		uint32_t period = int(value["period"].GetDouble());
		return new bgfx_frame_parameter(std::move(name), type, period);
	}
	else if (type == parameter_type::PARAM_WINDOW)
	{
		return new bgfx_window_parameter(std::move(name), type, effects.window_index());
	}
	else if (type == parameter_type::PARAM_TIME)
	{
		auto limit = float(value["limit"].GetDouble());
		return new bgfx_time_parameter(std::move(name), type, limit);
	}
	else
	{
		READER_CHECK(false, "%sUnknown parameter type '%s'\n", prefix, value["type"].GetString());
	}

	return nullptr;
}

bool bgfx_parameter::validate_parameters(const Value& value, const std::string &prefix)
{
	if (!READER_CHECK(value.HasMember("name"), "%sMust have string value 'name'\n", prefix)) return false;
	if (!READER_CHECK(value["name"].IsString(), "%sValue 'name' must be a string\n", prefix)) return false;
	if (!READER_CHECK(value.HasMember("type"), "%sMust have string value 'type'\n", prefix)) return false;
	if (!READER_CHECK(value["type"].IsString(), "%sValue 'type' must be a string\n", prefix)) return false;
	if (!READER_CHECK(!value.HasMember("period") || value["period"].IsNumber(), "%sValue 'period' must be numeric\n", prefix)) return false;
	if (!READER_CHECK(!value.HasMember("limit") || value["limit"].IsNumber(), "%sValue 'period' must be numeric\n", prefix)) return false;
	return true;
}
