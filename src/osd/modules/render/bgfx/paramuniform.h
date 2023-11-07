// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  paramuniform.h - BGFX shader effect parametric uniform
//
//============================================================

#ifndef MAME_RENDER_BGFX_PARAMUNIFORM_H
#define MAME_RENDER_BGFX_PARAMUNIFORM_H

#pragma once

#include "entryuniform.h"

class bgfx_parameter;

class bgfx_param_uniform : public bgfx_entry_uniform
{
public:
	bgfx_param_uniform(bgfx_uniform* uniform, bgfx_parameter* param);

	static bgfx_entry_uniform* from_json(const Value& value, const std::string &prefix, bgfx_uniform* uniform, std::map<std::string, bgfx_parameter*>& params);

	virtual void bind() override;

private:
	static bool validate_parameters(const Value& value, const std::string &prefix);

	bgfx_parameter* m_param;
};

#endif // MAME_RENDER_BGFX_PARAMUNIFORM_H
