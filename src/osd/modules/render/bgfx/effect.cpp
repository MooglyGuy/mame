// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  effect.cpp - BGFX screen-space shader effect
//
//============================================================

#include "effect.h"

#include "emu.h"
#include "render.h"
#include "rendlay.h"
#include "screen.h"

#include <bx/timer.h>

#include "clear.h"
#include "effectmanager.h"
#include "entryuniform.h"
#include "parameter.h"
#include "slider.h"
#include "target.h"
#include "targetmanager.h"
#include "texturemanager.h"
#include "valueuniform.h"
#include "vertex.h"

#include "modules/osdwindow.h"

bgfx_effect::bgfx_effect(
		std::string &&name,
		std::string &&author,
		bool transform,
		target_manager& targets,
		std::vector<bgfx_slider*> &&sliders,
		std::vector<bgfx_parameter*> &&params,
		std::vector<bgfx_effect_entry*> &&entries,
		std::vector<bgfx_target*> &&target_list,
		std::uint32_t screen_index)
	: m_name(std::move(name))
	, m_author(std::move(author))
	, m_transform(transform)
	, m_targets(targets)
	, m_sliders(std::move(sliders))
	, m_params(std::move(params))
	, m_entries(std::move(entries))
	, m_target_list(std::move(target_list))
	, m_current_time(0)
	, m_screen_index(screen_index)
	, m_has_converter(false)
{
	for (bgfx_target* target : m_target_list)
	{
		m_target_map[target->name()] = target;
		m_target_names.push_back(target->name());
	}
}

bgfx_effect::~bgfx_effect()
{
	for (bgfx_slider* slider : m_sliders)
	{
		delete slider;
	}
	for (bgfx_parameter* param : m_params)
	{
		delete param;
	}
	for (bgfx_effect_entry* entry : m_entries)
	{
		delete entry;
	}
	for (bgfx_target* target : m_target_list)
	{
		m_targets.destroy_target(target->name(), m_screen_index);
	}
}

std::unique_ptr<bgfx_effect> bgfx_effect::from_json(const Value& value, const std::string &prefix, effect_manager &effects,
	uint32_t screen_index, uint16_t user_prescale, uint16_t max_prescale_size)
{
	if (!validate_parameters(value, prefix))
	{
		return nullptr;
	}

	std::string name = value["name"].GetString();
	std::string author = value["author"].GetString();

	// Parse sliders
	std::vector<bgfx_slider*> sliders;
	if (value.HasMember("sliders"))
	{
		const Value& slider_array = value["sliders"];
		for (uint32_t i = 0; i < slider_array.Size(); i++)
		{
			std::vector<bgfx_slider*> expanded_sliders = bgfx_slider::from_json(slider_array[i], prefix + "sliders[" + std::to_string(i) + "]: ", effects, screen_index);
			if (expanded_sliders.size() == 0)
			{
				return nullptr;
			}
			for (bgfx_slider* slider : expanded_sliders)
			{
				sliders.push_back(slider);
			}
		}
	}

	// Parse whether the screen container is transformed by the effect's shaders
	bool transform = false;
	if (value.HasMember("transform"))
	{
		transform = value["transform"].GetBool();
	}

	// Map sliders
	std::map<std::string, bgfx_slider*> slider_map;
	for (bgfx_slider* slider : sliders)
	{
		slider_map[slider->name()] = slider;
	}

	// Parse parameters
	std::vector<bgfx_parameter*> parameters;
	if (value.HasMember("parameters"))
	{
		const Value& param_array = value["parameters"];
		for (uint32_t i = 0; i < param_array.Size(); i++)
		{
			bgfx_parameter* parameter = bgfx_parameter::from_json(param_array[i], prefix + "parameters[" + std::to_string(i) + "]; ", effects);
			if (parameter == nullptr)
			{
				return nullptr;
			}
			parameters.push_back(parameter);
		}
	}

	// Map parameters
	std::map<std::string, bgfx_parameter*> param_map;
	for (bgfx_parameter* param : parameters)
	{
		param_map[param->name()] = param;
	}

	// Create targets
	std::vector<bgfx_target*> target_list;
	if (value.HasMember("targets"))
	{
		const Value& target_array = value["targets"];
		// TODO: Move into its own reader
		for (uint32_t i = 0; i < target_array.Size(); i++)
		{
			bgfx_target* target = bgfx_target::from_json(target_array[i], prefix + "targets[" + std::to_string(i) + "]: ", effects, screen_index, user_prescale, max_prescale_size);
			if (target == nullptr)
			{
				return nullptr;
			}
			target_list.push_back(target);
		}
	}

	// Parse effect entries
	std::vector<bgfx_effect_entry*> entries;
	if (value.HasMember("passes"))
	{
		const Value& entry_array = value["passes"];
		for (uint32_t i = 0; i < entry_array.Size(); i++)
		{
			bgfx_effect_entry* entry = bgfx_effect_entry::from_json(entry_array[i], prefix + "passes[" + std::to_string(i) + "]: ", effects, slider_map, param_map, screen_index);
			if (entry == nullptr)
			{
				return nullptr;
			}
			entries.push_back(entry);
		}
	}

	return std::make_unique<bgfx_effect>(
			std::move(name),
			std::move(author),
			transform,
			effects.targets(),
			std::move(sliders),
			std::move(parameters),
			std::move(entries),
			std::move(target_list),
			screen_index);
}

void bgfx_effect::repopulate_targets()
{
	for (size_t i = 0; i < m_target_names.size(); i++)
	{
		bgfx_target* target = m_targets.target(m_screen_index, m_target_names[i]);
		if (target != nullptr) {
			m_target_list[i] = target;
		}
	}
}

void bgfx_effect::process(effect_manager::screen_prim &prim, int view, int screen, texture_manager& textures, osd_window& window)
{
	screen_device_enumerator screen_iterator(window.machine().root_device());
	screen_device* screen_device = screen_iterator.byindex(screen);

	uint16_t screen_count(window.target()->current_view().visible_screen_count());
	uint16_t screen_width = prim.m_quad_width;
	uint16_t screen_height = prim.m_quad_height;
	uint32_t rotation_type =
		(window.target()->orientation() & ROT90)  == ROT90  ? 1 :
		(window.target()->orientation() & ROT180) == ROT180 ? 2 :
		(window.target()->orientation() & ROT270) == ROT270 ? 3 : 0;
	bool orientation_swap_xy = (window.machine().system().flags & ORIENTATION_SWAP_XY) == ORIENTATION_SWAP_XY;
	bool rotation_swap_xy = (window.target()->orientation() & ORIENTATION_SWAP_XY) == ORIENTATION_SWAP_XY;
	bool swap_xy = orientation_swap_xy ^ rotation_swap_xy;

	float screen_scale_x =  1.0f;
	float screen_scale_y = 1.0f;
	float screen_offset_x = 0.0f;
	float screen_offset_y = 0.0f;
	if (screen_device != nullptr)
	{
		render_container &screen_container = screen_device->container();
		screen_scale_x = 1.0f / screen_container.xscale();
		screen_scale_y = 1.0f / screen_container.yscale();
		screen_offset_x = -screen_container.xoffset();
		screen_offset_y = -screen_container.yoffset();
	}

	int current_view = view;
	for (size_t i = 0; i < m_entries.size(); i++)
	{
		if (!m_entries[i]->skip())
		{
			m_entries[i]->submit(current_view, prim, textures, screen_count, screen_width, screen_height, screen_scale_x, screen_scale_y, screen_offset_x, screen_offset_y,
				rotation_type, swap_xy, screen);
			current_view++;
		}
	}

	m_current_time = bx::getHPCounter();
	static int64_t last = m_current_time;
	const int64_t frameTime = m_current_time - last;
	last = m_current_time;
	const auto freq = double(bx::getHPFrequency());
	const double toMs = 1000.0 / freq;
	const double frameTimeInSeconds = (double)frameTime / 1000000.0;

	for (bgfx_parameter* param : m_params)
	{
		param->tick(frameTimeInSeconds * toMs);
	}
}

uint32_t bgfx_effect::applicable_passes()
{
	int applicable_passes = 0;
	for (bgfx_effect_entry* entry : m_entries)
	{
		if (!entry->skip())
		{
			applicable_passes++;
		}
	}

	return applicable_passes;
}

void bgfx_effect::insert_shader(uint32_t index, bgfx_shader *shader, const bool apply_tint, std::string name, std::string source, effect_manager &effects)
{
	auto *clear = new clear_state(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL, 0, 1.0f, 0);
	std::vector<bgfx_suppressor*> suppressors;

	std::vector<bgfx_input_pair*> inputs;
	std::vector<std::string> available_textures;
	inputs.push_back(new bgfx_input_pair(0, "s_tex", source,  available_textures, "", effects, m_screen_index));
	inputs.push_back(new bgfx_input_pair(1, "s_pal", "palette", available_textures, "", effects, m_screen_index));

	std::vector<bgfx_entry_uniform*> uniforms;
	float value = 1.0f;
	float values[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
	uniforms.push_back(new bgfx_value_uniform(new bgfx_uniform("s_tex", bgfx::UniformType::Sampler), &value, 1));
	uniforms.push_back(new bgfx_value_uniform(new bgfx_uniform("s_pal", bgfx::UniformType::Sampler), &value, 1));
	uniforms.push_back(new bgfx_value_uniform(new bgfx_uniform("u_tex_size0", bgfx::UniformType::Vec4), values, 4));
	uniforms.push_back(new bgfx_value_uniform(new bgfx_uniform("u_tex_size1", bgfx::UniformType::Vec4), values, 4));
	uniforms.push_back(new bgfx_value_uniform(new bgfx_uniform("u_inv_tex_size0", bgfx::UniformType::Vec4), values, 4));
	uniforms.push_back(new bgfx_value_uniform(new bgfx_uniform("u_inv_tex_size1", bgfx::UniformType::Vec4), values, 4));
	uniforms.push_back(new bgfx_value_uniform(new bgfx_uniform("u_tex_bounds0", bgfx::UniformType::Vec4), values, 4));
	uniforms.push_back(new bgfx_value_uniform(new bgfx_uniform("u_tex_bounds1", bgfx::UniformType::Vec4), values, 4));
	uniforms.push_back(new bgfx_value_uniform(new bgfx_uniform("u_inv_tex_bounds0", bgfx::UniformType::Vec4), values, 4));
	uniforms.push_back(new bgfx_value_uniform(new bgfx_uniform("u_inv_tex_bounds1", bgfx::UniformType::Vec4), values, 4));

	m_entries.insert(m_entries.begin() + index, new bgfx_effect_entry(name, shader, clear, suppressors, inputs, uniforms, m_targets, "screen", apply_tint));

	const uint32_t screen_width = effects.targets().width(TARGET_STYLE_GUEST, m_screen_index);
	const uint32_t screen_height = effects.targets().height(TARGET_STYLE_GUEST, m_screen_index);
	m_targets.destroy_target("screen", m_screen_index);
	m_targets.create_target("screen", bgfx::TextureFormat::BGRA8, screen_width, screen_height, 1, 1, TARGET_STYLE_GUEST, true, false, 1, m_screen_index);
}

bool bgfx_effect::validate_parameters(const Value& value, const std::string &prefix)
{
	if (!READER_CHECK(value.HasMember("name"), "%sMust have string value 'name'\n", prefix)) return false;
	if (!READER_CHECK(value["name"].IsString(), "%sValue 'name' must be a string\n", prefix)) return false;
	if (!READER_CHECK(value.HasMember("author"), "%sMust have string value 'author'\n", prefix)) return false;
	if (!READER_CHECK(value["author"].IsString(), "%sValue 'author' must be a string\n", prefix)) return false;
	if (!READER_CHECK(value.HasMember("passes"), "%sMust have array value 'passes'\n", prefix)) return false;
	if (!READER_CHECK(value["passes"].IsArray(), "%sValue 'passes' must be an array\n", prefix)) return false;
	if (!READER_CHECK(!value.HasMember("sliders") || value["sliders"].IsArray(), "%sValue 'sliders' must be an array\n", prefix)) return false;
	if (!READER_CHECK(!value.HasMember("parameters") || value["parameters"].IsArray(), "%sValue 'parameters' must be an array\n", prefix)) return false;
	if (!READER_CHECK(!value.HasMember("targets") || value["targets"].IsArray(), "%sValue 'targets' must be an array\n", prefix)) return false;
	return true;
}
