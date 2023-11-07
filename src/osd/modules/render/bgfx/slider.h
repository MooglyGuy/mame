// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  slider.h - BGFX shader parameter slider
//
//============================================================

#ifndef MAME_RENDER_BGFX_SLIDER_H
#define MAME_RENDER_BGFX_SLIDER_H

#pragma once

#include "statereader.h"

#include <bgfx/bgfx.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class effect_manager;
struct slider_state;

class running_machine;

class bgfx_slider : public state_reader
{
public:
	enum slider_type
	{
		SLIDER_INT_ENUM,
		SLIDER_FLOAT,
		SLIDER_INT,
		SLIDER_COLOR,
		SLIDER_VEC2
	};

	enum screen_type
	{
		SLIDER_SCREEN_TYPE_NONE = 0,
		SLIDER_SCREEN_TYPE_RASTER = 1,
		SLIDER_SCREEN_TYPE_VECTOR = 2,
		SLIDER_SCREEN_TYPE_VECTOR_OR_RASTER = SLIDER_SCREEN_TYPE_VECTOR | SLIDER_SCREEN_TYPE_RASTER,
		SLIDER_SCREEN_TYPE_LCD = 4,
		SLIDER_SCREEN_TYPE_LCD_OR_RASTER = SLIDER_SCREEN_TYPE_LCD | SLIDER_SCREEN_TYPE_RASTER,
		SLIDER_SCREEN_TYPE_LCD_OR_VECTOR = SLIDER_SCREEN_TYPE_LCD | SLIDER_SCREEN_TYPE_VECTOR,
		SLIDER_SCREEN_TYPE_ANY = SLIDER_SCREEN_TYPE_RASTER | SLIDER_SCREEN_TYPE_VECTOR | SLIDER_SCREEN_TYPE_LCD
	};

	bgfx_slider(running_machine& machine, std::string &&name, float min, float def, float max, float step, slider_type type, screen_type screen, std::string format, std::string description, std::vector<std::string>& strings);
	virtual ~bgfx_slider();

	static std::vector<bgfx_slider*> from_json(const Value& value, const std::string &prefix, effect_manager& effects, uint32_t screen_index);

	int32_t update(std::string *str, int32_t newval);

	// Getters
	const std::string &name() const { return m_name; }
	slider_type type() const { return m_type; }
	float value() const { return m_value; }
	float uniform_value() const { return float(m_value); }
	float min_value() const { return m_min; }
	float default_value() const { return m_default; }
	float max_value() const { return m_max; }
	slider_state *core_slider() const { return m_slider_state.get(); }
	size_t size() const { return get_size_for_type(m_type); }
	static size_t get_size_for_type(slider_type type);

	// Setters
	void import(float val);

protected:
	static bool get_values(const Value& value, const std::string &prefix, const std::string &name, float* values, const int count);
	static bool validate_parameters(const Value& value, const std::string &prefix);

	std::unique_ptr<slider_state> create_core_slider();
	int32_t as_int() const { return int32_t(floor(m_value / m_step + 0.5f)); }

	std::string     m_name;
	float           m_min;
	float           m_default;
	float           m_max;
	float           m_step;
	slider_type     m_type;
	screen_type     m_screen_type;
	std::string     m_format;
	std::string     m_description;
	std::vector<std::string> m_strings;
	float           m_value;
	std::unique_ptr<slider_state> m_slider_state;
	running_machine&m_machine;

	static const int TYPE_COUNT = 5;
	static const string_to_enum TYPE_NAMES[TYPE_COUNT];
	static const int SCREEN_COUNT = 11;
	static const string_to_enum SCREEN_NAMES[SCREEN_COUNT];
};

#endif // MAME_RENDER_BGFX_SLIDER_H
