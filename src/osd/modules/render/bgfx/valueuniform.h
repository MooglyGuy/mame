// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  valueuniform.h - BGFX shader effect fixed uniform
//
//  Represents the mapping between a fixed value and an effect
//  shader uniform for a given entry
//
//============================================================

#ifndef MAME_RENDER_BGFX_VALUEUNIFORM_H
#define MAME_RENDER_BGFX_VALUEUNIFORM_H

#pragma once

#include "entryuniform.h"

class bgfx_value_uniform : public bgfx_entry_uniform
{
public:
	bgfx_value_uniform(bgfx_uniform* uniform, const float* values, const int count);

	static bgfx_entry_uniform* from_json(const Value& value, const std::string &prefix, bgfx_uniform* uniform);

	virtual void bind() override;

private:
	static bool validate_parameters(const Value& value, const std::string &prefix);

	float       m_values[4];
	const int   m_count;
};

#endif // MAME_RENDER_BGFX_VALUEUNIFORM_H
