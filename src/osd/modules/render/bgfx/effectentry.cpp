// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  effectentry.cpp - BGFX shader post-processing node
//
//  Represents a single entry in a list of post-processing
//  passes to be applied to a screen quad or buffer.
//
//============================================================

#include "effectmanager.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <cmath>

#include "effectentry.h"

#include "shader.h"
#include "clear.h"
#include "texture.h"
#include "target.h"
#include "entryuniform.h"
#include "texturemanager.h"
#include "vertex.h"
#include "suppressor.h"

#include "emucore.h"
#include "fileio.h"
#include "render.h"

#include "osdcore.h"
#include "modules/lib/osdobj_common.h"

bgfx_effect_entry::bgfx_effect_entry(std::string name, bgfx_shader* shader, clear_state* clear, std::vector<bgfx_suppressor*> suppressors, std::vector<bgfx_input_pair*> inputs,
	std::vector<bgfx_entry_uniform*> uniforms, target_manager& targets, std::string output, bool apply_tint)
	: m_name(name)
	, m_shader(shader)
	, m_clear(clear)
	, m_suppressors(suppressors)
	, m_inputs(inputs)
	, m_uniforms(uniforms)
	, m_targets(targets)
	, m_output(output)
	, m_apply_tint(apply_tint)
{
}

bgfx_effect_entry::~bgfx_effect_entry()
{
	for (bgfx_input_pair* input : m_inputs)
	{
		delete input;
	}
	m_inputs.clear();
	for (bgfx_entry_uniform* uniform : m_uniforms)
	{
		delete uniform;
	}
	m_uniforms.clear();
	if (m_clear)
	{
		delete m_clear;
	}
}

bgfx_effect_entry* bgfx_effect_entry::from_json(
		const Value &value,
		const std::string &prefix,
		effect_manager &effects,
		std::map<std::string, bgfx_slider*> &sliders,
		std::map<std::string, bgfx_parameter*> &params,
		uint32_t screen_index)
{
	if (!validate_parameters(value, prefix))
	{
		osd_printf_error("Effect entry failed validation.\n");
		return nullptr;
	}

	bgfx_shader* shader = effects.shaders().get_or_load_shader(effects.options(), value["shader"].GetString());
	if (shader == nullptr)
	{
		return nullptr;
	}

	std::string name = value["name"].GetString();

	std::vector<bgfx_input_pair*> inputs;
	if (value.HasMember("input"))
	{
		const Value& input_array = value["input"];
		for (uint32_t i = 0; i < input_array.Size(); i++)
		{
			const Value& input = input_array[i];
			if (!READER_CHECK(input.HasMember("sampler"), "%sinput[%u]: Must have string value 'sampler' (what sampler are we binding to?)\n", prefix, i)) return nullptr;
			if (!READER_CHECK(input["sampler"].IsString(), "%sinput[%u]: Value 'sampler' must be a string\n", prefix, i)) return nullptr;
			bool has_texture = input.HasMember("texture");
			bool has_target = input.HasMember("target");
			bool has_option = input.HasMember("option");
			if (!READER_CHECK(has_texture || has_target || has_option, "%sinput[%u]: Must have string value 'target', 'texture' or 'option' (what source are we using?)\n", prefix, i)) return nullptr;
			if (!READER_CHECK(!has_texture || input["texture"].IsString(), "%sinput[%u]: Value 'texture' must be a string\n", prefix, i)) return nullptr;
			if (!READER_CHECK(!has_target || input["target"].IsString(), "%sinput[%u]: Value 'target' must be a string\n", prefix, i)) return nullptr;
			if (!READER_CHECK(!has_option || input["option"].IsString(), "%sinput[%u]: Value 'option' must be a string\n", prefix, i)) return nullptr;
			if (!READER_CHECK(has_target || !input.HasMember("bilinear") || input["bilinear"].IsBool(), "%sinput[%u]: Value 'bilinear' must be a boolean\n", prefix, i)) return nullptr;
			if (!READER_CHECK(has_target || !input.HasMember("clamp") || input["clamp"].IsBool(), "%sinput[%u]: Value 'clamp' must be a boolean\n", prefix, i)) return nullptr;
			if (!READER_CHECK(has_texture || has_option || !input.HasMember("selection") || input["selection"].IsString(), "%sinput[%u]: Value 'selection' must be a string\n", prefix, i)) return nullptr;
			bool bilinear = get_bool(input, "bilinear", true);
			bool clamp = get_bool(input, "clamp", false);
			std::string selection = get_string(input, "selection", "");

			std::vector<std::string> texture_names;
			std::string texture_name = "";
			if (has_texture || has_option)
			{
				if (has_texture)
				{
					texture_name = input["texture"].GetString();
				}
				if (has_option)
				{
					std::string option = input["option"].GetString();

					texture_name = effects.options().value(option.c_str());
				}

				if (texture_name != "" && texture_name != "screen" && texture_name != "palette")
				{
					if (selection == "")
					{
						// create texture for specified file name
						uint32_t flags = bilinear ? 0u : (BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT);
						flags |= clamp ? (BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP) : 0u;
						bgfx_texture* texture = effects.textures().create_png_texture(effects.options().art_path(), texture_name, texture_name, flags, screen_index);
						if (texture == nullptr)
						{
							return nullptr;
						}
					}
					else
					{
						// create texture for specified file name
						uint32_t flags = bilinear ? 0u : (BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT);
						flags |= clamp ? (BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP) : 0u;
						bgfx_texture* texture = effects.textures().create_png_texture(effects.options().art_path(), texture_name, texture_name, flags, screen_index);
						if (texture == nullptr)
						{
							return nullptr;
						}

						// get directory of file
						const size_t last_slash = texture_name.rfind('/');
						const std::string file_directory = last_slash != std::string::npos ? texture_name.substr(0, last_slash) : std::string();
						file_enumerator directory_path(effects.options().art_path());
						while (const osd::directory::entry *entry = directory_path.next(file_directory.empty() ? nullptr : file_directory.c_str()))
						{
							if (entry->type == osd::directory::entry::entry_type::FILE)
							{
								std::string file(entry->name);
								std::string extension(".png");

								// split into file name and extension
								std::string file_name;
								std::string file_extension;
								const size_t last_dot = file.rfind('.');
								if (last_dot != std::string::npos)
								{
									file_name = file.substr(0, last_dot);
									file_extension = file.substr(last_dot, file.length() - last_dot);
								}

								std::string file_path;
								if (file_directory == "")
								{
									file_path = file;
								}
								else
								{
									file_path = file_directory + PATH_SEPARATOR + file;
								}

								// check for .png extension
								if (file_extension == extension && std::find(texture_names.begin(), texture_names.end(), file_path) == texture_names.end())
								{
									// create textures for all files containd in the path of the specified file name
									uint32_t flags = bilinear ? 0u : (BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT);
									flags |= clamp ? (BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP) : 0u;
									bgfx_texture* texture = effects.textures().create_png_texture(effects.options().art_path(), file_path, file_path, flags, screen_index);
									if (texture == nullptr)
									{
										return nullptr;
									}
									texture_names.push_back(file_path);
								}
							}
						}
					}
				}
			}
			else if (has_target)
			{
				texture_name = input["target"].GetString();
			}
			else
			{
				return nullptr;
			}

			std::string sampler = input["sampler"].GetString();
			auto* input_pair = new bgfx_input_pair(i, sampler, texture_name, texture_names, selection, effects, screen_index);
			inputs.push_back(input_pair);
		}
	}

	// Parse whether or not to apply screen tint in this pass
	bool applytint = get_bool(value, "applytint", false);

	// Parse uniforms
	std::vector<bgfx_entry_uniform*> uniforms;
	if (value.HasMember("uniforms"))
	{
		const Value& uniform_array = value["uniforms"];
		for (uint32_t i = 0; i < uniform_array.Size(); i++)
		{
			bgfx_entry_uniform* uniform = bgfx_entry_uniform::from_json(uniform_array[i], prefix + "uniforms[" + std::to_string(i) + "]: ", shader, sliders, params);
			if (uniform == nullptr)
			{
				for (bgfx_entry_uniform* existing_uniform : uniforms) delete existing_uniform;
				return nullptr;
			}
			uniforms.push_back(uniform);
		}
	}

	std::vector<bgfx_suppressor*> suppressors;
	if (value.HasMember("disablewhen"))
	{
		const Value& suppressor_array = value["disablewhen"];
		for (uint32_t i = 0; i < suppressor_array.Size(); i++)
		{
			bgfx_suppressor* suppressor = bgfx_suppressor::from_json(suppressor_array[i], prefix, sliders);
			if (suppressor == nullptr)
			{
				for (bgfx_entry_uniform* uniform : uniforms) delete uniform;
				for (bgfx_suppressor* existing_suppressor : suppressors) delete existing_suppressor;
				return nullptr;
			}
			suppressors.push_back(suppressor);
		}
	}

	// Parse clear state
	clear_state* clear = nullptr;
	if (value.HasMember("clear"))
	{
		clear = clear_state::from_json(value["clear"], prefix + "clear state: ");
	}
	else
	{
		clear = new clear_state(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x00000000, 1.0f, 0);
	}

	std::string output = value["output"].GetString();
	return new bgfx_effect_entry(name, shader, clear, suppressors, inputs, uniforms, effects.targets(), output, applytint);
}

void bgfx_effect_entry::submit(int view, effect_manager::screen_prim &prim, texture_manager& textures, uint16_t screen_count, uint16_t screen_width, uint16_t screen_height,
	float screen_scale_x, float screen_scale_y, float screen_offset_x, float screen_offset_y, uint32_t rotation_type, bool swap_xy, int32_t screen)
{
	uint16_t view_width = screen_width;
	uint16_t view_height = screen_height;
	if (!setup_view(textures, view, screen_width, screen_height, screen, view_width, view_height))
	{
		return;
	}

	for (bgfx_input_pair* input : m_inputs)
	{
		input->bind(m_shader, screen);
	}

	uint32_t tint = 0xffffffff;
	if (m_apply_tint)
	{
		const uint8_t a = (uint8_t)std::round(prim.m_prim->color.a * 255);
		const uint8_t r = (uint8_t)std::round(prim.m_prim->color.r * 255);
		const uint8_t g = (uint8_t)std::round(prim.m_prim->color.g * 255);
		const uint8_t b = (uint8_t)std::round(prim.m_prim->color.b * 255);
		tint = (a << 24) | (b << 16) | (g << 8) | r;
	}

	bgfx::TransientVertexBuffer buffer;
	put_screen_buffer(view_width, view_height, tint, &buffer);
	bgfx::setVertexBuffer(0, &buffer);

	setup_auto_uniforms(prim, textures, screen_count, view_width, view_height, screen_width, screen_height, screen_scale_x, screen_scale_y, screen_offset_x, screen_offset_y,
		rotation_type, swap_xy, screen);

	for (bgfx_entry_uniform* uniform : m_uniforms)
	{
		if (uniform->name() != "s_tex")
		{
			uniform->bind();
		}
	}

	m_shader->submit(view);

	if (m_targets.target(screen, m_output) != nullptr)
	{
		m_targets.target(screen, m_output)->page_flip();
	}
}

bool bgfx_effect_entry::validate_parameters(const Value& value, const std::string &prefix)
{
	if (!READER_CHECK(value.HasMember("shader"), "%sMust have string value 'shader' (what shader does this entry use?)\n", prefix)) return false;
	if (!READER_CHECK(value["shader"].IsString(), "%sValue 'shader' must be a string\n", prefix)) return false;
	if (!READER_CHECK(value.HasMember("name"), "%sMust have string value 'name' (what name does this entry have?)\n", prefix)) return false;
	if (!READER_CHECK(value["name"].IsString(), "%sValue 'name' must be a string\n", prefix)) return false;
	if (!READER_CHECK(value.HasMember("output"), "%sMust have string value 'offset' (what target are we rendering to?)\n", prefix)) return false;
	if (!READER_CHECK(value["output"].IsString(), "%sValue 'output' must be a string\n", prefix)) return false;
	if (!READER_CHECK(!value.HasMember("input") || value["input"].IsArray(), "%sValue 'input' must be an array\n", prefix)) return false;
	if (!READER_CHECK(!value.HasMember("uniforms") || value["uniforms"].IsArray(), "%sValue 'uniforms' must be an array\n", prefix)) return false;
	if (!READER_CHECK(!value.HasMember("disablewhen") || value["disablewhen"].IsArray(), "%sValue 'disablewhen' must be an array\n", prefix)) return false;
	if (!READER_CHECK(!value.HasMember("applytint") || value["applytint"].IsBool(), "%sValue 'applytint' must be a bool\n", prefix)) return false;
	return true;
}

void bgfx_effect_entry::setup_auto_uniforms(effect_manager::screen_prim &prim, texture_manager& textures, uint16_t screen_count, uint16_t view_width, uint16_t view_height,
	uint16_t screen_width, uint16_t screen_height, float screen_scale_x, float screen_scale_y, float screen_offset_x, float screen_offset_y,
	uint32_t rotation_type, bool swap_xy, int32_t screen)
{
	setup_viewsize_uniforms(view_width, view_height);
	setup_screensize_uniforms(textures, screen_width, screen_height, screen);
	setup_screenscale_uniforms(screen_scale_x, screen_scale_y);
	setup_screenoffset_uniforms(screen_offset_x, screen_offset_y);
	setup_screencount_uniforms(screen_count);
	setup_sourcesize_uniform(prim);
	setup_targetsize_uniform(screen);
	setup_targetscale_uniform(screen);
	setup_rotationtype_uniform(rotation_type);
	setup_swapxy_uniform(swap_xy);
	setup_quaddims_uniform(prim);
	setup_screenindex_uniform(screen);
}

void bgfx_effect_entry::setup_viewsize_uniforms(uint16_t view_width, uint16_t view_height)
{
	float width(view_width);
	float height(view_height);
	bgfx_uniform* view_dims = m_shader->uniform("u_view_dims");
	if (view_dims != nullptr)
	{
		float values[2] = { width, height };
		view_dims->set(values, sizeof(float) * 2);
	}

	bgfx_uniform* inv_view_dims = m_shader->uniform("u_inv_view_dims");
	if (inv_view_dims != nullptr)
	{
		float values[2] = { -1.0f / width, 1.0f / height };
		inv_view_dims->set(values, sizeof(float) * 2);
	}
}

void bgfx_effect_entry::setup_screensize_uniforms(texture_manager& textures, uint16_t screen_width, uint16_t screen_height, int32_t screen)
{
	float width = screen_width;
	float height = screen_height;
	if (m_inputs.size() > 0)
	{
		std::string name = m_inputs[0]->texture() + std::to_string(screen);
		width = float(textures.provider(name)->width());
		height = float(textures.provider(name)->height());
	}

	bgfx_uniform* screen_dims = m_shader->uniform("u_screen_dims");
	if (screen_dims != nullptr)
	{
		float values[2] = { width, height };
		screen_dims->set(values, sizeof(float) * 2);
	}

	bgfx_uniform* inv_screen_dims = m_shader->uniform("u_inv_screen_dims");
	if (inv_screen_dims != nullptr)
	{
		float values[2] = { 1.0f / width, 1.0f / height };
		inv_screen_dims->set(values, sizeof(float) * 2);
	}
}

void bgfx_effect_entry::setup_screenscale_uniforms(float screen_scale_x, float screen_scale_y)
{
	bgfx_uniform* screen_scale = m_shader->uniform("u_screen_scale");
	if (screen_scale != nullptr)
	{
		float values[2] = { screen_scale_x, screen_scale_y };
		screen_scale->set(values, sizeof(float) * 2);
	}
}

void bgfx_effect_entry::setup_screenoffset_uniforms(float screen_offset_x, float screen_offset_y)
{
	bgfx_uniform* screen_offset = m_shader->uniform("u_screen_offset");
	if (screen_offset != nullptr)
	{
		float values[2] = { screen_offset_x, screen_offset_y };
		screen_offset->set(values, sizeof(float) * 2);
	}
}

void bgfx_effect_entry::setup_screencount_uniforms(uint16_t screen_count)
{
	bgfx_uniform* u_screen_count = m_shader->uniform("u_screen_count");
	if (u_screen_count != nullptr)
	{
		float values[1] = { float(screen_count) };
		u_screen_count->set(values, sizeof(float));
	}
}

void bgfx_effect_entry::setup_sourcesize_uniform(effect_manager::screen_prim &prim) const
{
	bgfx_uniform* source_dims = m_shader->uniform("u_source_dims");
	if (source_dims != nullptr)
	{
		source_dims->set(&prim.m_tex_width, sizeof(float) * 2);
	}
}

void bgfx_effect_entry::setup_targetsize_uniform(int32_t screen) const
{
	bgfx_uniform* target_dims = m_shader->uniform("u_target_dims");
	if (target_dims != nullptr)
	{
		bgfx_target* output = m_targets.target(screen, m_output);
		if (output != nullptr)
		{
			float values[2] = { float(output->width()), float(output->height()) };
			target_dims->set(values, sizeof(float) * 2);
		}
	}
}

void bgfx_effect_entry::setup_targetscale_uniform(int32_t screen) const
{
	bgfx_uniform* target_scale = m_shader->uniform("u_target_scale");
	if (target_scale != nullptr)
	{
		bgfx_target* output = m_targets.target(screen, m_output);
		if (output != nullptr)
		{
			float values[2] = { float(output->scale()), float(output->scale()) };
			target_scale->set(values, sizeof(float) * 2);
		}
	}
}

void bgfx_effect_entry::setup_rotationtype_uniform(uint32_t rotation_type) const
{
	bgfx_uniform* rotation_type_uniform = m_shader->uniform("u_rotation_type");
	if (rotation_type_uniform != nullptr)
	{
		float values[1] = { float(rotation_type) };
		rotation_type_uniform->set(values, sizeof(float));
	}
}

void bgfx_effect_entry::setup_swapxy_uniform(bool swap_xy) const
{
	bgfx_uniform* swap_xy_uniform = m_shader->uniform("u_swap_xy");
	if (swap_xy_uniform != nullptr)
	{
		float values[1] = { swap_xy ? 1.0f : 0.0f };
		swap_xy_uniform->set(values, sizeof(float));
	}
}

void bgfx_effect_entry::setup_quaddims_uniform(effect_manager::screen_prim &prim) const
{
	bgfx_uniform* quad_dims_uniform = m_shader->uniform("u_quad_dims");
	if (quad_dims_uniform != nullptr)
	{
		float values[2] = { float(prim.m_quad_width), float(prim.m_quad_height) };
		quad_dims_uniform->set(values, sizeof(float) * 2);
	}
}

void bgfx_effect_entry::setup_screenindex_uniform(int32_t screen) const
{
	bgfx_uniform* screen_index = m_shader->uniform("u_screen_index");
	if (screen_index != nullptr)
	{
		float values[1] = { float(screen) };
		screen_index->set(values, sizeof(float));
	}
}

bool bgfx_effect_entry::setup_view(texture_manager &textures, int view, uint16_t screen_width, uint16_t screen_height, int32_t screen,
	uint16_t &out_view_width, uint16_t &out_view_height) const
{
	bgfx::FrameBufferHandle handle = BGFX_INVALID_HANDLE;
	uint16_t width = screen_width;
	uint16_t height = screen_height;
	if (m_targets.target(screen, m_output) != nullptr)
	{
		bgfx_target* output = m_targets.target(screen, m_output);
		if (output->width() == 0)
		{
			return false;
		}
		handle = output->target();
		width = output->width();
		height = output->height();
	}

	bgfx::setViewFrameBuffer(view, handle);
	bgfx::setViewRect(view, 0, 0, width, height);
	out_view_width = width;
	out_view_height = height;

	const bgfx::Caps* caps = bgfx::getCaps();

	std::string name = m_inputs[0]->texture() + std::to_string(screen);
	const float right_ratio = float(textures.provider(name)->width()) / textures.provider(name)->rowpixels();

	float projMat[16];
	bx::mtxOrtho(projMat, 0.0f, right_ratio, 1.0f, 0.0f, 0.0f, 100.0f, 0.0f, caps->homogeneousDepth);
	bgfx::setViewTransform(view, nullptr, projMat);

	m_clear->bind(view);
	return true;
}

void bgfx_effect_entry::put_screen_buffer(uint16_t screen_width, uint16_t screen_height, uint32_t screen_tint, bgfx::TransientVertexBuffer* buffer) const
{
	if (6 == bgfx::getAvailTransientVertexBuffer(6, ScreenVertex::ms_decl))
	{
		bgfx::allocTransientVertexBuffer(buffer, 6, ScreenVertex::ms_decl);
	}
	else
	{
		return;
	}

	auto* vertex = reinterpret_cast<ScreenVertex*>(buffer->data);

	float x[4] = { 0, 1, 0, 1 };
	float y[4] = { 0, 0, 1, 1 };
	float u[4] = { 0, 1, 0, 1 };
	float v[4] = { 0, 0, 1, 1 };

	bgfx::RendererType::Enum renderer_type = bgfx::getRendererType();
	if (renderer_type == bgfx::RendererType::OpenGL || renderer_type == bgfx::RendererType::OpenGLES)
	{
		v[0] = v[1] = 1;
		v[2] = v[3] = 0;
	}

	vertex[0].m_x = x[0];
	vertex[0].m_y = y[0];
	vertex[0].m_z = 0;
	vertex[0].m_rgba = screen_tint;
	vertex[0].m_u = u[0];
	vertex[0].m_v = v[0];

	vertex[1].m_x = x[1];
	vertex[1].m_y = y[1];
	vertex[1].m_z = 0;
	vertex[1].m_rgba = screen_tint;
	vertex[1].m_u = u[1];
	vertex[1].m_v = v[1];

	vertex[2].m_x = x[3];
	vertex[2].m_y = y[3];
	vertex[2].m_z = 0;
	vertex[2].m_rgba = screen_tint;
	vertex[2].m_u = u[3];
	vertex[2].m_v = v[3];

	vertex[3].m_x = x[3];
	vertex[3].m_y = y[3];
	vertex[3].m_z = 0;
	vertex[3].m_rgba = screen_tint;
	vertex[3].m_u = u[3];
	vertex[3].m_v = v[3];

	vertex[4].m_x = x[2];
	vertex[4].m_y = y[2];
	vertex[4].m_z = 0;
	vertex[4].m_rgba = screen_tint;
	vertex[4].m_u = u[2];
	vertex[4].m_v = v[2];

	vertex[5].m_x = x[0];
	vertex[5].m_y = y[0];
	vertex[5].m_z = 0;
	vertex[5].m_rgba = screen_tint;
	vertex[5].m_u = u[0];
	vertex[5].m_v = v[0];
}

bool bgfx_effect_entry::skip()
{
	if (m_suppressors.size() == 0)
	{
		return false;
	}

	// Group all AND/OR'd results together and OR them together (hack for now)
	// TODO: Make this a bit more logical

	bool or_suppress = false;
	int and_count = 0;
	int and_suppressed = 0;
	for (bgfx_suppressor* suppressor : m_suppressors)
	{
		if (suppressor->combine() == bgfx_suppressor::combine_mode::COMBINE_AND)
		{
			and_count++;
			if (suppressor->suppress())
			{
				and_suppressed++;
			}
		}
		else if (suppressor->combine() == bgfx_suppressor::combine_mode::COMBINE_OR)
		{
			or_suppress |= suppressor->suppress();
		}
	}

	return (and_count != 0 && and_suppressed == and_count) || or_suppress;
}
