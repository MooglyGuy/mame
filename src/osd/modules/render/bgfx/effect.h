// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  effect.h - BGFX screen-space post-effect
//
//============================================================

#ifndef MAME_RENDER_BGFX_EFFECT_H
#define MAME_RENDER_BGFX_EFFECT_H

#pragma once

#include "effectentry.h"
#include "statereader.h"

#include <string>
#include <vector>
#include <map>

class bgfx_slider;
class bgfx_parameter;
class texture_manager;
class target_manager;
class bgfx_target;
class osd_window;

/*class mamefx_renderer
{
public:
};

class mamefx_effect
{
public:
	mamefx_effect(std::string &&name, std::string &&author, target_manager& targets, std::vector<mamefx_slider*> &&sliders, std::vector<mamefx_parameter*> &&params, std::vector<mamefx_effect_entry*> &&entries, std::vector<mamefx_target*> &&target_list, uint32_t screen_index);
	virtual ~mamefx_effect();

	void process_screen(effect_manager::screen_prim &prim, int screen, texture_manager& textures, osd_window &window);
	void repopulate_targets();

	// Getters
	const std::string &name() const { return m_name; }
	std::vector<mamefx_slider*>& sliders() { return m_sliders; }
	std::vector<mamefx_effect_entry*>& entries() { return m_entries; }
	uint32_t applicable_passes();
	bool transform() const { return m_transform; }
	bool has_converter() const { return m_has_converter; }
	bool has_adjuster() const { return m_has_adjuster; }

	// Setters
	void set_has_converter(bool has_converter) { m_has_converter = has_converter; }
	void set_has_adjuster(bool has_adjuster) { m_has_adjuster = has_adjuster; }
	void insert_shader(uint32_t index, mamefx_shader *shader, const bool apply_tint, std::string name, std::string source, effect_manager &effects);
};*/

class bgfx_effect : public state_reader//: public mamefx_effect
{
public:
	bgfx_effect(std::string &&name, std::string &&author, bool transform, target_manager& targets, std::vector<bgfx_slider*> &&sliders, std::vector<bgfx_parameter*> &&params, std::vector<bgfx_effect_entry*> &&entries, std::vector<bgfx_target*> &&target_list, uint32_t screen_index);
	~bgfx_effect();

	static std::unique_ptr<bgfx_effect> from_json(const Value& value, const std::string &prefix, effect_manager& effects, uint32_t screen_index, uint16_t user_prescale, uint16_t max_prescale_size);

	void process(effect_manager::screen_prim &prim, int view, int screen, texture_manager& textures, osd_window &window);
	void repopulate_targets();

	// Getters
	const std::string &name() const { return m_name; }
	std::vector<bgfx_slider*>& sliders() { return m_sliders; }
	std::vector<bgfx_effect_entry*>& entries() { return m_entries; }
	uint32_t applicable_passes();
	bool transform() const { return m_transform; }
	bool has_converter() const { return m_has_converter; }
	bool has_adjuster() const { return m_has_adjuster; }

	// Setters
	void set_has_converter(bool has_converter) { m_has_converter = has_converter; }
	void set_has_adjuster(bool has_adjuster) { m_has_adjuster = has_adjuster; }
	void insert_shader(uint32_t index, bgfx_shader *shader, const bool apply_tint, std::string name, std::string source, effect_manager &effects);

private:
	static bool validate_parameters(const Value& value, const std::string &prefix);

	std::string                         m_name;
	std::string                         m_author;
	bool                                m_transform;
	target_manager&                     m_targets;
	std::vector<bgfx_slider*>           m_sliders;
	std::vector<bgfx_parameter*>        m_params;
	std::vector<bgfx_effect_entry*>     m_entries;
	std::vector<bgfx_target*>           m_target_list;
	std::vector<std::string>            m_target_names;
	std::map<std::string, bgfx_target*> m_target_map;
	int64_t                             m_current_time;
	uint32_t                            m_screen_index;
	bool                                m_has_converter;
	bool                                m_has_adjuster;
};

#endif // MAME_RENDER_BGFX_EFFECT_H
