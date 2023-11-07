// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  shader.h - BGFX shader material to be applied to a mesh
//
//============================================================

#ifndef MAME_RENDER_BGFX_SHADER_H
#define MAME_RENDER_BGFX_SHADER_H

#pragma once

#include "statereader.h"

#include <bgfx/bgfx.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

class bgfx_uniform;
class osd_options;
class shaderprog_manager;

class bgfx_shader : public state_reader
{
public:
	bgfx_shader(std::string &&name, uint64_t state, bgfx::ShaderHandle vertex_shader, bgfx::ShaderHandle fragment_shader, std::vector<std::unique_ptr<bgfx_uniform> > &uniforms);
	~bgfx_shader();

	static std::unique_ptr<bgfx_shader> from_json(const std::string &name, const Value& value, const std::string &prefix, const osd_options &options, shaderprog_manager& shaderprogs);
	static bool validate_value(const Value& value, const std::string &prefix, const osd_options &options);

	void submit(int view, uint64_t blend = ~0ULL);
	bgfx_uniform *uniform(const std::string &name);
	bool is_valid() const { return m_program_handle.idx != bgfx::kInvalidHandle; }

private:
	static uint64_t blend_from_json(const Value& value);
	static uint64_t depth_from_json(const Value& value, const std::string &prefix);
	static uint64_t cull_from_json(const Value& value);
	static std::unique_ptr<bgfx_uniform> uniform_from_json(const Value& value, const std::string &prefix);
	static uint64_t write_from_json(const Value& value);

	static bool get_base_shader_data(const Value& value, const std::string &prefix, uint64_t &flags, std::string &vertex_name, std::string &fragment_name,
		std::vector<std::unique_ptr<bgfx_uniform> > &uniforms);
	static bool get_shaderprog_data(const Value& value, const osd_options &options, shaderprog_manager &shaderprogs, std::string &vertex_name, bgfx::ShaderHandle &vertex_shader,
		std::string &fragment_name, bgfx::ShaderHandle &fragment_shader);

	static bool validate_parameters(const Value& value, const std::string &prefix);
	static bool validate_uniform(const Value& value, const std::string &prefix);

	std::string                          m_name;
	uint64_t                             m_state;
	bgfx::ProgramHandle                  m_program_handle;
	std::map<std::string, std::unique_ptr<bgfx_uniform> > m_uniforms;

	static const int BLEND_EQUATION_COUNT = 7;
	static const string_to_enum BLEND_EQUATION_NAMES[BLEND_EQUATION_COUNT];
	static const int BLEND_FUNCTION_COUNT = 16;
	static const string_to_enum BLEND_FUNCTION_NAMES[BLEND_FUNCTION_COUNT];
	static const int DEPTH_FUNCTION_COUNT = 8;
	static const string_to_enum DEPTH_FUNCTION_NAMES[DEPTH_FUNCTION_COUNT];
	static const int CULL_MODE_COUNT = 5;
	static const string_to_enum CULL_MODE_NAMES[CULL_MODE_COUNT];
	static const int UNIFORM_TYPE_COUNT = 4;
	static const string_to_enum UNIFORM_TYPE_NAMES[UNIFORM_TYPE_COUNT];
};

#endif // MAME_RENDER_BGFX_SHADER_H
