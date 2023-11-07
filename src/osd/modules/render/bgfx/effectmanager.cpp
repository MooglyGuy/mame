// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  effectmanager.cpp - BGFX shader effect manager
//
//  Provides loading for BGFX shader effects, defined
//  by effect.h
//
//============================================================

#include "effectmanager.h"

#include <bx/readerwriter.h>
#include <bx/file.h>

#include "emucore.h"
#include "render.h"
#include "../frontend/mame/ui/slider.h"

#include "modules/lib/osdobj_common.h"
#include "modules/osdwindow.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include "bgfxutil.h"

#include "effect.h"
#include "slider.h"
#include "target.h"
#include "texture.h"

#include "sliderdirtynotifier.h"

#include "util/path.h"
#include "util/unicode.h"
#include "util/xmlfile.h"

#include "osdcore.h"
#include "osdfile.h"

#include <algorithm>
#include <locale>


using namespace rapidjson;

effect_manager::screen_prim::screen_prim(render_primitive *prim)
{
	m_prim = prim;
	m_screen_width = uint16_t(floorf(prim->get_full_quad_width() + 0.5f));
	m_screen_height = uint16_t(floorf(prim->get_full_quad_height() + 0.5f));
	m_quad_width = uint16_t(floorf(prim->get_quad_width() + 0.5f));
	m_quad_height = uint16_t(floorf(prim->get_quad_height() + 0.5f));
	m_tex_width = prim->texture.width;
	m_tex_height = prim->texture.height;
	m_rowpixels = prim->texture.rowpixels;
	m_palette_length = prim->texture.palette_length;
	m_flags = prim->flags;
}

effect_manager::effect_manager(
		running_machine& machine,
		const osd_options& options,
		texture_manager& textures,
		target_manager& targets,
		shader_manager& shaders,
		uint32_t window_index,
		slider_dirty_notifier& slider_notifier,
		uint16_t user_prescale,
		uint16_t max_prescale_size)
	: m_machine(machine)
	, m_options(options)
	, m_textures(textures)
	, m_targets(targets)
	, m_shaders(shaders)
	, m_window_index(window_index)
	, m_user_prescale(user_prescale)
	, m_max_prescale_size(max_prescale_size)
	, m_slider_notifier(slider_notifier)
	, m_screen_count(0)
	, m_default_effect_index(-1)
{
	m_converters.clear();
	refresh_available_effects();
	parse_effect_selections(options.bgfx_screen_effects());
	init_texture_converters();
}

effect_manager::~effect_manager()
{
	destroy_effects();
}

void effect_manager::init_texture_converters()
{
	m_converters.push_back(nullptr);
	m_converters.push_back(m_shaders.get_or_load_shader(m_options, "misc/texconv_palette16"));
	m_converters.push_back(m_shaders.get_or_load_shader(m_options, "misc/texconv_rgb32"));
	m_converters.push_back(nullptr);
	m_converters.push_back(m_shaders.get_or_load_shader(m_options, "misc/texconv_yuy16"));
	m_adjuster = m_shaders.get_or_load_shader(m_options, "misc/bcg_adjust");
}

void effect_manager::get_default_effect_info(std::string &out_effect_name, int32_t &out_effect_index)
{
	if (m_default_effect_index == -1)
	{
		out_effect_index = EFFECT_NONE;
		out_effect_name = "";
		return;
	}

	out_effect_index = m_default_effect_index;
	out_effect_name = "default";
	return;
}

void effect_manager::refresh_available_effects()
{
	m_available_effects.clear();
	m_available_effects.emplace_back("none", "");

	find_available_effects(util::path_concat(m_options.bgfx_path(), "effects"), "");
	std::collate<wchar_t> const &coll = std::use_facet<std::collate<wchar_t> >(std::locale());
	std::sort(
			m_available_effects.begin(),
			m_available_effects.end(),
			[&coll] (effect_desc const &x, effect_desc const &y) -> bool
			{
				if (x.m_name == "none")
					return y.m_name != "none";
				else if (y.m_name == "none")
					return false;
				else if (x.m_name == "default")
					return y.m_name != "default";
				else if (y.m_name == "default")
					return false;
				std::wstring const xstr = wstring_from_utf8(x.m_name);
				std::wstring const ystr = wstring_from_utf8(y.m_name);
				return coll.compare(xstr.data(), xstr.data() + xstr.size(), ystr.data(), ystr.data() + ystr.size()) < 0;
			});

	if (m_default_effect_index == -1)
	{
		for (size_t i = 0; i < m_available_effects.size(); i++)
		{
			if (m_available_effects[i].m_name == "default")
			{
				m_default_effect_index = int32_t(i);
			}
		}
	}

	destroy_unloaded_effects();
}

void effect_manager::destroy_unloaded_effects()
{
	// O(shaders*available_effects), but we don't care because asset reloading happens rarely
	for (int i = 0; i < m_effect_names.size(); i++)
	{
		const std::string &name = m_effect_names[i];
		if (name.length() > 0)
		{
			for (effect_desc desc : m_available_effects)
			{
				if (desc.m_name == name)
				{
					delete m_screen_effects[i];
					m_screen_effects[i] = nullptr;
					get_default_effect_info(m_effect_names[i], m_current_effect[i]);
					break;
				}
			}
		}
	}
}

void effect_manager::find_available_effects(std::string_view root, std::string_view path)
{
	osd::directory::ptr directory = osd::directory::open(path.empty() ? std::string(root) : util::path_concat(root, path));
	if (directory)
	{
		for (const osd::directory::entry *entry = directory->read(); entry; entry = directory->read())
		{
			if (entry->type == osd::directory::entry::entry_type::FILE)
			{
				const std::string_view name(entry->name);
				const std::string_view extension(".json");

				// Does the name has at least one character in addition to ".json"?
				if (name.length() > extension.length())
				{
					size_t start = name.length() - extension.length();
					const std::string_view test_segment = name.substr(start, extension.length());

					// Does it end in .json?
					if (test_segment == extension)
					{
						m_available_effects.emplace_back(std::string(name.substr(0, start)), std::string(path));
					}
				}
			}
			else if (entry->type == osd::directory::entry::entry_type::DIR)
			{
				const std::string_view name = entry->name;
				if ((name != ".") && (name != ".."))
				{
					if (path.empty())
						find_available_effects(root, name);
					else
						find_available_effects(root, util::path_concat(path, name));
				}
			}
		}
	}
}

std::unique_ptr<bgfx_effect> effect_manager::load_effect(std::string name, uint32_t screen_index)
{
	if (name.length() < 5 || (name.compare(name.length() - 5, 5, ".json") != 0))
	{
		name += ".json";
	}
	const std::string path = util::path_concat(m_options.bgfx_path(), "effects", name);

	bx::FileReader reader;
	if (!bx::open(&reader, path.c_str()))
	{
		osd_printf_warning("Unable to open effect file %s, falling back to no post processing\n", path);
		return nullptr;
	}

	const int32_t size(bx::getSize(&reader));

	bx::ErrorAssert err;
	std::unique_ptr<char []> data(new (std::nothrow) char [size + 1]);
	if (!data)
	{
		osd_printf_error("Out of memory reading effect file %s\n", path);
		bx::close(&reader);
		return nullptr;
	}

	bx::read(&reader, reinterpret_cast<void*>(data.get()), size, &err);
	bx::close(&reader);
	data[size] = 0;

	Document document;
	document.Parse<kParseCommentsFlag>(data.get());
	data.reset();

	if (document.HasParseError())
	{
		std::string error(GetParseError_En(document.GetParseError()));
		osd_printf_warning("Unable to parse effect %s. Errors returned:\n%s\n", path, error);
		return nullptr;
	}

	std::unique_ptr<bgfx_effect> effect = bgfx_effect::from_json(document, name + ": ", *this, screen_index, m_user_prescale, m_max_prescale_size);

	if (!effect)
	{
		osd_printf_warning("Unable to load effect %s, falling back to no post processing\n", path);
		return nullptr;
	}

	return effect;
}

void effect_manager::parse_effect_selections(std::string_view effect_str)
{
	std::vector<std::string_view> effect_names = split_option_string(effect_str);

	if (effect_names.empty())
		effect_names.push_back("default");

	while (m_current_effect.size() < effect_names.size())
	{
		m_screen_effects.emplace_back(nullptr);
		m_effect_names.emplace_back();
		m_current_effect.push_back(EFFECT_NONE);
	}

	for (size_t index = 0; index < effect_names.size(); index++)
	{
		size_t effect_index = 0;
		for (effect_index = 0; effect_index < m_available_effects.size(); effect_index++)
		{
			if (m_available_effects[effect_index].m_name == effect_names[index])
				break;
		}

		if (effect_index < m_available_effects.size())
		{
			m_current_effect[index] = effect_index;
			m_effect_names[index] = m_available_effects[effect_index].m_name;
		}
		else
		{
			m_current_effect[index] = EFFECT_NONE;
			m_effect_names[index] = "";
		}
	}
}

std::vector<std::string_view> effect_manager::split_option_string(std::string_view effect_str) const
{
	std::vector<std::string_view> effect_names;

	const uint32_t length = effect_str.length();
	uint32_t win = 0;
	uint32_t last_start = 0;
	for (uint32_t i = 0; i <= length; i++)
	{
		if (i == length || (effect_str[i] == ',') || (effect_str[i] == ':'))
		{
			if ((win == 0) || (win == m_window_index))
			{
				// treat an empty string as equivalent to "default"
				if (i > last_start)
					effect_names.push_back(effect_str.substr(last_start, i - last_start));
				else
					effect_names.push_back("default");
			}

			last_start = i + 1;
			if ((i < length) && (effect_str[i] == ':'))
			{
				// no point walking the rest of the string if this was our window
				if (win == m_window_index)
					break;

				// don't use first for all if more than one window is specified
				effect_names.clear();
				win++;
			}
		}
	}

	return effect_names;
}

void effect_manager::load_effects()
{
	for (size_t effect = 0; effect < m_current_effect.size() && effect < m_screen_effects.size(); effect++)
	{
		if (m_current_effect[effect] != EFFECT_NONE)
		{
			effect_desc& desc = m_available_effects[m_current_effect[effect]];
			m_effect_names[effect] = desc.m_name;
			m_screen_effects[effect] = load_effect(util::path_concat(desc.m_path, desc.m_name), uint32_t(effect)).release();
		}
	}
}

void effect_manager::destroy_effects()
{
	for (size_t index = 0; index < m_screen_effects.size(); index++)
	{
		if (m_screen_effects[index] != nullptr)
		{
			delete m_screen_effects[index];
			m_screen_effects[index] = nullptr;
		}
	}
}

void effect_manager::reload_effects()
{
	destroy_effects();
	load_effects();
}

bgfx_effect* effect_manager::screen_effect(uint32_t screen)
{
	if (screen >= m_screen_effects.size())
	{
		return m_screen_effects[m_screen_effects.size() - 1];
	}
	else
	{
		return m_screen_effects[screen];
	}
}

void effect_manager::process_screen_quad(uint32_t view, uint32_t screen, screen_prim &prim, osd_window& window)
{
	const bool any_targets_rebuilt = m_targets.update_target_sizes(screen, prim.m_tex_width, prim.m_tex_height, TARGET_STYLE_GUEST, m_user_prescale, m_max_prescale_size);
	if (any_targets_rebuilt)
	{
		for (bgfx_effect* effect : m_screen_effects)
		{
			if (effect != nullptr)
			{
				effect->repopulate_targets();
			}
		}
	}

	bgfx_effect* effect = screen_effect(screen);
	effect->process(prim, view, screen, m_textures, window);
	view += effect->applicable_passes();
}

uint32_t effect_manager::count_screens(render_primitive* prim)
{
	uint32_t screen_count = 0;
	while (prim != nullptr)
	{
		if (PRIMFLAG_GET_SCREENTEX(prim->flags))
		{
			if (screen_count < m_screen_prims.size())
			{
				m_screen_prims[screen_count] = prim;
			}
			else
			{
				m_screen_prims.push_back(prim);
			}
			screen_count++;
		}
		prim = prim->next();
	}

	if (screen_count > 0)
	{
		update_screen_count(screen_count);
		m_targets.update_screen_count(screen_count, m_user_prescale, m_max_prescale_size);
	}

	if (screen_count < m_screen_prims.size())
	{
		m_screen_prims.resize(screen_count);
	}

	return screen_count;
}

void effect_manager::update_screen_count(uint32_t screen_count)
{
	if (screen_count != m_screen_count)
	{
		m_slider_notifier.set_sliders_dirty();
		m_screen_count = screen_count;

		// Ensure we have one screen effect entry per screen
		while (m_screen_effects.size() < m_screen_count)
		{
			m_screen_effects.push_back(nullptr);

			int32_t effect_index = EFFECT_NONE;
			std::string effect_name;
			get_default_effect_info(effect_name, effect_index);
			m_effect_names.emplace_back(std::move(effect_name));
			m_current_effect.push_back(effect_index);
		}

		// Ensure we have a screen effect selection slider per screen
		while (m_selection_sliders.size() < m_screen_count)
		{
			create_selection_slider(m_selection_sliders.size());
		}

		load_effects();
	}
}

void effect_manager::set_current_effect(uint32_t screen, int32_t effect_index)
{
	if (effect_index < m_available_effects.size() && screen < m_current_effect.size() && screen < m_effect_names.size())
	{
		m_current_effect[screen] = effect_index;
		m_effect_names[screen] = m_available_effects[effect_index].m_name;
	}
}

int32_t effect_manager::slider_changed(int id, std::string *str, int32_t newval)
{
	if (newval != SLIDER_NOCHANGE)
	{
		set_current_effect(id, newval);

		std::vector<std::vector<float>> settings = slider_settings();
		reload_effects();
		restore_slider_settings(id, settings);

		m_slider_notifier.set_sliders_dirty();
	}

	if (str != nullptr)
	{
		*str = m_available_effects[m_current_effect[id]].m_name;
	}

	return m_current_effect[id];
}

void effect_manager::create_selection_slider(uint32_t screen_index)
{
	if (screen_index < m_selection_sliders.size())
	{
		return;
	}

	int32_t minval = 0;
	int32_t defval = m_current_effect[screen_index];
	int32_t maxval = m_available_effects.size() - 1;
	int32_t incval = 1;

	using namespace std::placeholders;
	auto state = std::make_unique<slider_state>(
			util::string_format("Window %1$u, Screen %2$u Effect", m_window_index, screen_index),
			minval, defval, maxval, incval,
			std::bind(&effect_manager::slider_changed, this, screen_index, _1, _2));

	ui::menu_item item(ui::menu_item_type::SLIDER, state.get());
	item.set_text(state->description);
	m_selection_sliders.emplace_back(item);
	m_core_sliders.emplace_back(std::move(state));
}

uint32_t effect_manager::update_screen_textures(uint32_t view, render_primitive *starting_prim, osd_window& window)
{
	if (!count_screens(starting_prim))
		return 0;

	for (int screen = 0; screen < m_screen_prims.size(); screen++)
	{
		screen_prim &prim = m_screen_prims[screen];
		uint16_t tex_width(prim.m_tex_width);
		uint16_t tex_height(prim.m_tex_height);

		bgfx_texture* texture = screen < m_screen_textures.size() ? m_screen_textures[screen] : nullptr;
		bgfx_texture* palette = screen < m_screen_palettes.size() ? m_screen_palettes[screen] : nullptr;

		const uint32_t src_format = (prim.m_flags & PRIMFLAG_TEXFORMAT_MASK) >> PRIMFLAG_TEXFORMAT_SHIFT;
		const bool needs_conversion = m_converters[src_format] != nullptr;
		const bool needs_adjust = prim.m_prim->texture.palette != nullptr && src_format != TEXFORMAT_PALETTE16;
		const std::string screen_index = std::to_string(screen);
		const std::string source_name = "source" + screen_index;
		const std::string screen_name = "screen" + screen_index;
		const std::string palette_name = "palette" + screen_index;
		const std::string &full_name = (needs_conversion || needs_adjust) ? source_name : screen_name;
		if (texture && (texture->width() != tex_width || texture->height() != tex_height))
		{
			m_textures.remove_provider(full_name);
			m_textures.remove_provider(palette_name);
			texture = nullptr;
			palette = nullptr;
		}

		bgfx::TextureFormat::Enum dst_format = bgfx::TextureFormat::BGRA8;
		uint16_t pitch = prim.m_rowpixels;
		int width_div_factor = 1;
		int width_mul_factor = 1;
		const bgfx::Memory* mem = bgfx_util::mame_texture_data_to_bgfx_texture_data(dst_format, prim.m_flags & PRIMFLAG_TEXFORMAT_MASK,
			prim.m_rowpixels, prim.m_prim->texture.width_margin, tex_height, prim.m_prim->texture.palette, prim.m_prim->texture.base, pitch, width_div_factor, width_mul_factor);

		if (!texture)
		{
			uint32_t flags = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
			if (!PRIMFLAG_GET_TEXWRAP(prim.m_flags))
				flags |= BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
			auto newtex = std::make_unique<bgfx_texture>(full_name, dst_format, tex_width, prim.m_prim->texture.width_margin, tex_height, mem, flags, pitch, prim.m_rowpixels, width_div_factor, width_mul_factor);
			texture = newtex.get();
			m_textures.add_provider(full_name, std::move(newtex));

			if (prim.m_prim->texture.palette)
			{
				uint16_t palette_width = uint16_t(std::min(prim.m_palette_length, 256U));
				uint16_t palette_height = uint16_t(std::max((prim.m_palette_length + 255) / 256, 1U));
				m_palette_temp.resize(palette_width * palette_height * 4);
				memcpy(&m_palette_temp[0], prim.m_prim->texture.palette, prim.m_palette_length * 4);
				const bgfx::Memory *palmem = bgfx::copy(&m_palette_temp[0], palette_width * palette_height * 4);
				auto newpal = std::make_unique<bgfx_texture>(palette_name, bgfx::TextureFormat::BGRA8, palette_width, 0, palette_height, palmem, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT, palette_width * 4);
				palette = newpal.get();
				m_textures.add_provider(palette_name, std::move(newpal));
			}

			while (screen >= m_screen_textures.size())
			{
				m_screen_textures.emplace_back(nullptr);
			}
			m_screen_textures[screen] = texture;

			while (screen >= m_screen_palettes.size())
			{
				m_screen_palettes.emplace_back(nullptr);
			}
			if (palette)
			{
				m_screen_palettes[screen] = palette;
			}
		}
		else
		{
			texture->update(mem, pitch, prim.m_prim->texture.width_margin);

			if (prim.m_prim->texture.palette)
			{
				uint16_t palette_width = uint16_t(std::min(prim.m_palette_length, 256U));
				uint16_t palette_height = uint16_t(std::max((prim.m_palette_length + 255) / 256, 1U));
				const uint32_t palette_size = palette_width * palette_height * 4;
				m_palette_temp.resize(palette_size);
				memcpy(&m_palette_temp[0], prim.m_prim->texture.palette, prim.m_palette_length * 4);
				const bgfx::Memory *palmem = bgfx::copy(&m_palette_temp[0], palette_size);

				if (palette)
				{
					palette->update(palmem);
				}
				else
				{
					auto newpal = std::make_unique<bgfx_texture>(palette_name, bgfx::TextureFormat::BGRA8, palette_width, 0, palette_height, palmem, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT, palette_width * 4);
					palette = newpal.get();
					m_textures.add_provider(palette_name, std::move(newpal));
					while (screen >= m_screen_palettes.size())
					{
						m_screen_palettes.emplace_back(nullptr);
					}
					m_screen_palettes[screen] = palette;
				}
			}
		}

		const bool has_tint = (prim.m_prim->color.a != 1.0f) || (prim.m_prim->color.r != 1.0f) || (prim.m_prim->color.g != 1.0f) || (prim.m_prim->color.b != 1.0f);
		bgfx_effect* effect = screen_effect(screen);
		if (effect && needs_adjust && !effect->has_adjuster())
		{
			const bool apply_tint = !needs_conversion && has_tint;
			effect->insert_shader(effect->has_converter() ? 1 : 0, m_adjuster, apply_tint, "XXadjust", needs_conversion ? "screen" : "source", *this);
			effect->set_has_adjuster(true);
		}
		if (effect && needs_conversion && !effect->has_converter())
		{
			effect->insert_shader(0, m_converters[src_format], has_tint, "XXconvert", "source", *this);
			effect->set_has_converter(true);
		}
	}

	return m_screen_prims.size();
}

uint32_t effect_manager::process_screen_effects(uint32_t view, osd_window& window)
{
	// Process each screen as necessary
	uint32_t used_views = 0;
	uint32_t screen_index = 0;
	for (screen_prim &prim : m_screen_prims)
	{
		if (m_current_effect[screen_index] == EFFECT_NONE || screen_effect(screen_index) == nullptr)
		{
			screen_index++;
			continue;
		}

		uint16_t screen_width = prim.m_screen_width;
		uint16_t screen_height = prim.m_screen_height;
		if (window.swap_xy())
		{
			std::swap(screen_width, screen_height);
		}

		const bool any_targets_rebuilt = m_targets.update_target_sizes(screen_index, screen_width, screen_height, TARGET_STYLE_NATIVE, m_user_prescale, m_max_prescale_size);
		if (any_targets_rebuilt)
		{
			for (bgfx_effect* effect : m_screen_effects)
			{
				if (effect != nullptr)
				{
					effect->repopulate_targets();
				}
			}
		}

		process_screen_quad(view + used_views, screen_index, prim, window);
		used_views += screen_effect(screen_index)->applicable_passes();

		screen_index++;
	}

	bgfx::setViewFrameBuffer(view + used_views, BGFX_INVALID_HANDLE);

	return used_views;
}

bool effect_manager::has_applicable_effect(uint32_t screen)
{
	return (screen < m_screen_count) && (m_current_effect[screen] != EFFECT_NONE) && m_screen_effects[screen];
}

bool effect_manager::needs_sliders()
{
	return (m_screen_count > 0) && (m_available_effects.size() > 1);
}

void effect_manager::restore_slider_settings(int32_t id, std::vector<std::vector<float>>& settings)
{
	if (!needs_sliders())
	{
		return;
	}

	for (size_t index = 0; index < m_screen_effects.size() && index < m_screen_count; index++)
	{
		if (index == id)
		{
			continue;
		}

		bgfx_effect* effect = m_screen_effects[index];
		if (effect == nullptr)
		{
			continue;
		}

		const std::vector<bgfx_slider*> &effect_sliders = effect->sliders();
		for (size_t slider = 0; slider < effect_sliders.size(); slider++)
		{
			effect_sliders[slider]->import(settings[index][slider]);
		}
	}
}

void effect_manager::load_config(util::xml::data_node const &windownode)
{
	// treat source INI files or more specific as higher priority than CFG
	// FIXME: leaky abstraction - this depends on a front-end implementation detail
	bool const persist = windownode.get_attribute_int("persist", 1) != 0;
	bool const default_effects = (OPTION_PRIORITY_NORMAL + 5) > m_options.get_entry(OSDOPTION_BGFX_SCREEN_EFFECTS)->priority();
	bool const explicit_effects = !persist && !default_effects && *m_options.bgfx_screen_effects();

	// if effects weren't explicitly specified, restore the effects from the config file
	if (explicit_effects)
	{
		osd_printf_verbose(
				"BGFX: Ignoring effect selection from window 0 configuration due to explicitly specified effects\n",
				m_window_index);
	}
	else
	{
		bool changed = false;
		util::xml::data_node const *screennode = windownode.get_child("screen");
		while (screennode)
		{
			auto const index = screennode->get_attribute_int("index", -1);
			if ((0 <= index) && (m_screen_count > index))
			{
				char const *const effectname = screennode->get_attribute_string("effect", nullptr);
				if (effectname)
				{
					auto const found = std::find_if(
							m_available_effects.begin(),
							m_available_effects.end(),
							[&effectname] (auto const &avail) { return avail.m_name == effectname; });
					if (m_available_effects.end() != found)
					{
						auto const effectnum = found - m_available_effects.begin();
						if (effectnum != m_current_effect[index])
						{
							m_current_effect[index] = effectnum;
							changed = true;
						}
					}
				}
			}

			screennode = screennode->get_next_sibling("screen");
		}

		if (changed)
			reload_effects();
	}

	// now apply slider settings for screens with effects matching config
	util::xml::data_node const *screennode = windownode.get_child("screen");
	while (screennode)
	{
		auto const index = screennode->get_attribute_int("index", -1);
		if ((0 <= index) && (m_screen_count > index) && (m_screen_effects.size() > index))
		{
			bgfx_effect *const effect = m_screen_effects[index];
			char const *const effectname = screennode->get_attribute_string("effect", nullptr);
			if (effect && effectname && (m_available_effects[m_current_effect[index]].m_name == effectname))
			{
				auto const &sliders = effect->sliders();

				util::xml::data_node const *slidernode = screennode->get_child("slider");
				while (slidernode)
				{
					char const *const slidername = slidernode->get_attribute_string("name", nullptr);
					if (slidername)
					{
						auto const found = std::find_if(
								sliders.begin(),
								sliders.end(),
								[&slidername] (auto const &slider) { return slider->name() == slidername; });
						if (sliders.end() != found)
						{
							bgfx_slider &slider = **found;
							switch (slider.type())
							{
							case bgfx_slider::SLIDER_INT_ENUM:
							case bgfx_slider::SLIDER_INT:
								{
									slider_state const &core = *slider.core_slider();
									int32_t const val = slidernode->get_attribute_int("value", core.defval);
									slider.update(nullptr, std::clamp(val, core.minval, core.maxval));
								}
								break;
							default:
								{
									float const val = slidernode->get_attribute_float("value", slider.default_value());
									slider.import(std::clamp(val, slider.min_value(), slider.max_value()));
								}
							}
						}
					}

					slidernode = slidernode->get_next_sibling("slider");
				}
			}
		}
		screennode = screennode->get_next_sibling("screen");
	}
}

void effect_manager::save_config(util::xml::data_node &parentnode)
{
	if (!needs_sliders())
		return;

	util::xml::data_node *const windownode = parentnode.add_child("window", nullptr);
	windownode->set_attribute_int("index", m_window_index);

	for (size_t index = 0; index < m_screen_effects.size() && index < m_screen_count; index++)
	{
		bgfx_effect *const effect = m_screen_effects[index];
		if (!effect)
			continue;

		util::xml::data_node *const screennode = windownode->add_child("screen", nullptr);
		screennode->set_attribute_int("index", index);
		screennode->set_attribute("effect", m_available_effects[m_current_effect[index]].m_name.c_str());

		for (bgfx_slider *slider : effect->sliders())
		{
			auto const val = slider->update(nullptr, SLIDER_NOCHANGE);
			if (val == slider->core_slider()->defval)
				continue;

			util::xml::data_node *const slidernode = screennode->add_child("slider", nullptr);
			slidernode->set_attribute("name", slider->name().c_str());
			switch (slider->type())
			{
			case bgfx_slider::SLIDER_INT_ENUM:
			case bgfx_slider::SLIDER_INT:
				slidernode->set_attribute_int("value", val);
				break;
			default:
				slidernode->set_attribute_float("value", slider->value());
			}
		}
	}

	if (!windownode->get_first_child())
		windownode->delete_node();
}

std::vector<std::vector<float>> effect_manager::slider_settings()
{
	std::vector<std::vector<float>> curr;

	if (!needs_sliders())
	{
		return curr;
	}

	for (size_t index = 0; index < m_screen_effects.size() && index < m_screen_count; index++)
	{
		curr.push_back(std::vector<float>());

		bgfx_effect* effect = m_screen_effects[index];
		if (effect == nullptr)
		{
			continue;
		}

		const std::vector<bgfx_slider*> &effect_sliders = effect->sliders();
		for (bgfx_slider* slider : effect_sliders)
		{
			curr[index].push_back(slider->value());
		}
	}

	return curr;
}

std::vector<ui::menu_item> effect_manager::get_slider_list()
{
	std::vector<ui::menu_item> sliders;

	if (!needs_sliders())
	{
		return sliders;
	}

	for (size_t index = 0; index < m_screen_effects.size() && index < m_screen_count; index++)
	{
		bgfx_effect* effect = m_screen_effects[index];
		sliders.push_back(m_selection_sliders[index]);

		if (effect == nullptr)
		{
			continue;
		}

		const std::vector<bgfx_effect_entry*> &effect_entries = effect->entries();
		for (bgfx_effect_entry* entry : effect_entries)
		{
			const std::vector<bgfx_input_pair*> &entry_inputs = entry->inputs();
			for (bgfx_input_pair* input : entry_inputs)
			{
				std::vector<ui::menu_item> input_sliders = input->get_slider_list();
				for (ui::menu_item &slider : input_sliders)
				{
					sliders.emplace_back(slider);
				}
			}
		}

		const std::vector<bgfx_slider*> &effect_sliders = effect->sliders();
		for (bgfx_slider* slider : effect_sliders)
		{
			slider_state *const core_slider = slider->core_slider();

			ui::menu_item item(ui::menu_item_type::SLIDER, core_slider);
			item.set_text(core_slider->description);
			m_selection_sliders.emplace_back(item);

			sliders.emplace_back(std::move(item));
		}

		if (effect_sliders.size() > 0)
		{
			ui::menu_item item(ui::menu_item_type::SEPARATOR);
			item.set_text(MENU_SEPARATOR_ITEM);

			sliders.emplace_back(std::move(item));
		}
	}

	return sliders;
}
