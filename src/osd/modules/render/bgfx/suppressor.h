// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  suppressor.h - Conditionally suppress a bgfx effect entry
//  from being processed.
//
//============================================================

#ifndef MAME_RENDER_BGFX_SUPPRESSOR_H
#define MAME_RENDER_BGFX_SUPPRESSOR_H

#pragma once

#include <bgfx/bgfx.h>

#include "statereader.h"

#include <map>
#include <vector>

class bgfx_slider;

class bgfx_suppressor : public state_reader
{
public:
	enum condition_type
	{
		CONDITION_EQUAL,
		CONDITION_NOTEQUAL,

		CONDITION_COUNT
	};

	enum combine_mode {
		COMBINE_AND,
		COMBINE_OR,

		COMBINE_COUNT
	};

	bgfx_suppressor(std::vector<bgfx_slider*> &&sliders, uint32_t condition, combine_mode combine, void* value);
	~bgfx_suppressor();

	static bgfx_suppressor* from_json(const Value& value, const std::string &prefix, std::map<std::string, bgfx_slider*>& sliders);

	// Getters
	bool suppress();
	combine_mode combine() const { return m_combine; }

private:
	static bool get_values(const Value& value, std::string prefix, std::string name, int* values, const int count);
	static bool validate_parameters(const Value& value, const std::string &prefix);

	std::vector<bgfx_slider*>   m_sliders;
	uint32_t                    m_condition;
	combine_mode                m_combine;
	uint8_t*                    m_value;

	static const string_to_enum CONDITION_NAMES[CONDITION_COUNT];
	static const string_to_enum COMBINE_NAMES[COMBINE_COUNT];
};

#endif // MAME_RENDER_BGFX_SUPPRESSOR_H
