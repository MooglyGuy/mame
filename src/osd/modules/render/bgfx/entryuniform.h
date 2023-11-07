// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  entryuniform.h - BGFX shader effect uniform remapper
//
//  Represents the mapping between a fixed value, a slider, or
//  other dynamic parameter and an effect shader uniform
//
//============================================================

#ifndef MAME_RENDER_BGFX_ENTRYUNIFORM_H
#define MAME_RENDER_BGFX_ENTRYUNIFORM_H

#pragma once

#include "uniform.h"
#include "statereader.h"

#include <bgfx/bgfx.h>

#include <map>

class bgfx_slider;
class bgfx_shader;
class bgfx_parameter;

class bgfx_entry_uniform : public state_reader
{
public:
	bgfx_entry_uniform(bgfx_uniform* uniform) : m_uniform(uniform) { }
	virtual ~bgfx_entry_uniform() { }

	static bgfx_entry_uniform* from_json(const Value& value, const std::string &prefix, bgfx_shader* shader, std::map<std::string, bgfx_slider*>& sliders, std::map<std::string, bgfx_parameter*>& params);

	virtual void bind() = 0;
	const std::string &name() const { return m_uniform->name(); }

protected:
	static bool validate_parameters(const Value& value, const std::string &prefix);

	bgfx_uniform* m_uniform;
};

#endif // MAME_RENDER_BGFX_ENTRYUNIFORM_H
