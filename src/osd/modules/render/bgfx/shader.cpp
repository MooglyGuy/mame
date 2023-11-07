// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  shader.cpp - BGFX shader material to be applied to a mesh
//
//============================================================

#include "shader.h"

#include "shaderprogmanager.h"
#include "uniform.h"

#include "modules/osdmodule.h"
#include "osdcore.h"

#include <utility>


const bgfx_shader::string_to_enum bgfx_shader::BLEND_EQUATION_NAMES[bgfx_shader::BLEND_EQUATION_COUNT] = {
	{ "add",            BGFX_STATE_BLEND_EQUATION_ADD },
	{ "sub",            BGFX_STATE_BLEND_EQUATION_SUB },
	{ "subtract",       BGFX_STATE_BLEND_EQUATION_SUB },
	{ "revsub",         BGFX_STATE_BLEND_EQUATION_REVSUB },
	{ "revsubtract",    BGFX_STATE_BLEND_EQUATION_REVSUB },
	{ "min",            BGFX_STATE_BLEND_EQUATION_MIN },
	{ "max",            BGFX_STATE_BLEND_EQUATION_MAX }
};

const bgfx_shader::string_to_enum bgfx_shader::BLEND_FUNCTION_NAMES[bgfx_shader::BLEND_FUNCTION_COUNT] = {
	{ "0",              BGFX_STATE_BLEND_ZERO },
	{ "zero",           BGFX_STATE_BLEND_ZERO },
	{ "1",              BGFX_STATE_BLEND_ONE },
	{ "one",            BGFX_STATE_BLEND_ONE },
	{ "srccolor",       BGFX_STATE_BLEND_SRC_COLOR },
	{ "1-srccolor",     BGFX_STATE_BLEND_INV_SRC_COLOR },
	{ "invsrccolor",    BGFX_STATE_BLEND_INV_SRC_COLOR },
	{ "dstcolor",       BGFX_STATE_BLEND_DST_COLOR },
	{ "1-dstcolor",     BGFX_STATE_BLEND_INV_DST_COLOR },
	{ "invdstcolor",    BGFX_STATE_BLEND_INV_DST_COLOR },
	{ "srcalpha",       BGFX_STATE_BLEND_SRC_ALPHA },
	{ "1-srcalpha",     BGFX_STATE_BLEND_INV_SRC_ALPHA },
	{ "invsrcalpha",    BGFX_STATE_BLEND_INV_SRC_ALPHA },
	{ "dstalpha",       BGFX_STATE_BLEND_DST_ALPHA },
	{ "1-dstalpha",     BGFX_STATE_BLEND_INV_DST_ALPHA },
	{ "invdstalpha",    BGFX_STATE_BLEND_INV_DST_ALPHA }
};

const bgfx_shader::string_to_enum bgfx_shader::DEPTH_FUNCTION_NAMES[bgfx_shader::DEPTH_FUNCTION_COUNT] = {
	{ "never",      BGFX_STATE_DEPTH_TEST_NEVER },
	{ "less",       BGFX_STATE_DEPTH_TEST_LESS },
	{ "equal",      BGFX_STATE_DEPTH_TEST_EQUAL },
	{ "lequal",     BGFX_STATE_DEPTH_TEST_LEQUAL },
	{ "greater",    BGFX_STATE_DEPTH_TEST_GREATER },
	{ "notequal",   BGFX_STATE_DEPTH_TEST_NOTEQUAL },
	{ "gequal",     BGFX_STATE_DEPTH_TEST_GEQUAL },
	{ "always",     BGFX_STATE_DEPTH_TEST_ALWAYS }
};

const bgfx_shader::string_to_enum bgfx_shader::CULL_MODE_NAMES[bgfx_shader::CULL_MODE_COUNT] = {
	{ "none",             0 },
	{ "cw",               BGFX_STATE_CULL_CW },
	{ "clockwise",        BGFX_STATE_CULL_CW },
	{ "ccw",              BGFX_STATE_CULL_CCW },
	{ "counterclockwise", BGFX_STATE_CULL_CCW }
};

const bgfx_shader::string_to_enum bgfx_shader::UNIFORM_TYPE_NAMES[bgfx_shader::UNIFORM_TYPE_COUNT] = {
	{ "int",    bgfx::UniformType::Sampler },
	{ "vec4",   bgfx::UniformType::Vec4 },
	{ "mat3",   bgfx::UniformType::Mat3 },
	{ "mat4",   bgfx::UniformType::Mat4 }
};


bgfx_shader::bgfx_shader(std::string &&name, uint64_t state, bgfx::ShaderHandle vertex_shader, bgfx::ShaderHandle fragment_shader, std::vector<std::unique_ptr<bgfx_uniform> > &uniforms)
	: m_name(std::move(name))
	, m_state(state)
{
	m_program_handle = bgfx::createProgram(vertex_shader, fragment_shader, false);

	for (auto &uniform : uniforms)
	{
		const auto existing = m_uniforms.find(uniform->name());
		if (existing != m_uniforms.end())
		{
			osd_printf_verbose("Uniform %s appears to be duplicated in one or more shaders, please double-check the shader JSON files.\n", uniform->name());
			uniform.reset();
			continue;
		}
		uniform->create();
		m_uniforms.emplace(uniform->name(), std::move(uniform));
	}
}

bgfx_shader::~bgfx_shader()
{
	m_uniforms.clear();
	bgfx::destroy(m_program_handle);
}

std::unique_ptr<bgfx_shader> bgfx_shader::from_json(
		const std::string &name,
		const Value &value,
		const std::string &prefix,
		const osd_options &options,
		shaderprog_manager &shaderprogs)
{
	uint64_t flags = 0;
	std::string vertex_name;
	std::string fragment_name;
	std::vector<std::unique_ptr<bgfx_uniform> > uniforms;

	if (!get_base_shader_data(value, prefix, flags, vertex_name, fragment_name, uniforms))
	{
		return nullptr;
	}

	bgfx::ShaderHandle vertex_shader = BGFX_INVALID_HANDLE;
	bgfx::ShaderHandle fragment_shader = BGFX_INVALID_HANDLE;

	if (!get_shaderprog_data(value, options, shaderprogs, vertex_name, vertex_shader, fragment_name, fragment_shader))
	{
		return nullptr;
	}

	std::unique_ptr<bgfx_shader> shader(new bgfx_shader(std::string(name), flags, vertex_shader, fragment_shader, uniforms));
	if (!shader->is_valid())
		return nullptr;

	return shader;
}

void bgfx_shader::submit(int view, uint64_t blend)
{
	for (auto &[name, uniform] : m_uniforms)
	{
		uniform->upload();
	}

	const uint64_t final_state = (blend != ~0ULL) ? ((m_state & ~BGFX_STATE_BLEND_MASK) | blend) : m_state;

	bgfx::setState(final_state);
	bgfx::submit(view, m_program_handle);
}

bgfx_uniform* bgfx_shader::uniform(const std::string &name)
{
	const auto iter = m_uniforms.find(name);
	return (iter != m_uniforms.end()) ? iter->second.get() : nullptr;
}

uint64_t bgfx_shader::blend_from_json(const Value& value)
{
	uint64_t equation = get_enum_from_value(value, "equation", BGFX_STATE_BLEND_EQUATION_ADD, BLEND_EQUATION_NAMES, BLEND_EQUATION_COUNT);
	uint64_t srccolor = get_enum_from_value(value, "srcColor", BGFX_STATE_BLEND_ONE, BLEND_FUNCTION_NAMES, BLEND_FUNCTION_COUNT);
	uint64_t dstcolor = get_enum_from_value(value, "dstColor", BGFX_STATE_BLEND_ZERO, BLEND_FUNCTION_NAMES, BLEND_FUNCTION_COUNT);
	if (value.HasMember("srcAlpha") && value.HasMember("dstAlpha"))
	{
		uint64_t srcalpha = get_enum_from_value(value, "srcAlpha", BGFX_STATE_BLEND_ONE, BLEND_FUNCTION_NAMES, BLEND_FUNCTION_COUNT);
		uint64_t dstalpha = get_enum_from_value(value, "dstAlpha", BGFX_STATE_BLEND_ZERO, BLEND_FUNCTION_NAMES, BLEND_FUNCTION_COUNT);
		return BGFX_STATE_BLEND_EQUATION(equation) | BGFX_STATE_BLEND_FUNC_SEPARATE(srccolor, dstcolor, srcalpha, dstalpha);
	}
	return BGFX_STATE_BLEND_EQUATION(equation) | BGFX_STATE_BLEND_FUNC(srccolor, dstcolor);
}

uint64_t bgfx_shader::depth_from_json(const Value& value, const std::string &prefix)
{
	uint64_t write_enable = 0;
	if (value.HasMember("writeenable"))
	{
		if (!READER_CHECK(value["writeenable"].IsBool(), "%sValue 'writeenable' must be a boolean\n", prefix)) return 0;
		write_enable = value["writeenable"].GetBool() ? BGFX_STATE_WRITE_Z : 0;
	}

	uint64_t function = get_enum_from_value(value, "function", BGFX_STATE_DEPTH_TEST_ALWAYS, DEPTH_FUNCTION_NAMES, DEPTH_FUNCTION_COUNT);

	return write_enable | function;
}

uint64_t bgfx_shader::cull_from_json(const Value& value)
{
	return get_enum_from_value(value, "mode", BGFX_STATE_CULL_CCW, CULL_MODE_NAMES, CULL_MODE_COUNT);
}

uint64_t bgfx_shader::write_from_json(const Value& value)
{
	uint64_t rgb = get_bool(value, "rgb", false) ? BGFX_STATE_WRITE_RGB : 0;
	uint64_t alpha = get_bool(value, "alpha", false) ? BGFX_STATE_WRITE_A : 0;
	return rgb | alpha;
}

std::unique_ptr<bgfx_uniform> bgfx_shader::uniform_from_json(const Value& value, const std::string &prefix)
{
	if (!validate_uniform(value, prefix))
	{
		return nullptr;
	}
	const char* name = value["name"].GetString();

	bgfx::UniformType::Enum type = bgfx::UniformType::Enum(get_enum_from_value(value, "type", bgfx::UniformType::Vec4, UNIFORM_TYPE_NAMES, UNIFORM_TYPE_COUNT));
	const size_t type_size = bgfx_uniform::get_size_for_type(type);

	const Value& value_array = value["values"];
	const size_t array_size = value_array.Size() * sizeof(float);

	auto* data = reinterpret_cast<float*>(std::malloc(std::max(type_size, array_size)));

	unsigned int index = 0;
	for (; index < type_size / 4 && index < value_array.Size(); index++)
		data[index] = float(value_array[index].GetDouble());

	for (; index < type_size / 4; index++)
		data[index] = 0.0f;

	auto uniform = std::make_unique<bgfx_uniform>(name, type);
	uniform->set(data, type_size);
	std::free(data);

	return uniform;
}

bool bgfx_shader::get_base_shader_data(const Value& value, const std::string &prefix, uint64_t &flags,
		std::string &vertex_name, std::string &fragment_name, std::vector<std::unique_ptr<bgfx_uniform> > &uniforms)
{
	if (!validate_parameters(value, prefix))
	{
		return false;
	}

	uint64_t blend = 0;
	if (value.HasMember("blend"))
	{
		blend = blend_from_json(value["blend"]);
	}
	uint64_t depth = depth_from_json(value["depth"], prefix + "depth: ");
	uint64_t cull = cull_from_json(value["cull"]);
	uint64_t write = write_from_json(value["write"]);
	flags = blend | depth | cull | write;

	const Value& uniform_array = value["uniforms"];
	for (uint32_t i = 0; i < uniform_array.Size(); i++)
	{
		auto uniform = uniform_from_json(uniform_array[i], prefix + "uniforms[" + std::to_string(i) + "]: ");
		if (!uniform)
		{
			return false;
		}
		uniforms.emplace_back(std::move(uniform));
	}

	vertex_name = value["vertex"].GetString();

	if (value.HasMember("fragment"))
	{
		fragment_name = value["fragment"].GetString();
	}
	else if (value.HasMember("pixel"))
	{
		fragment_name = value["pixel"].GetString();
	}
	else
	{
		fragment_name = "";
	}

	return true;
}

bool bgfx_shader::get_shaderprog_data(const Value &value, const osd_options &options, shaderprog_manager &shaderprogs,
		std::string &vertex_name, bgfx::ShaderHandle &vertex_shader, std::string &fragment_name, bgfx::ShaderHandle &fragment_shader)
{
	vertex_shader = shaderprogs.load_shader_program(options, vertex_name);
	if (vertex_shader.idx == bgfx::kInvalidHandle)
	{
		return false;
	}

	fragment_shader = shaderprogs.load_shader_program(options, fragment_name);
	if (fragment_shader.idx == bgfx::kInvalidHandle)
	{
		return false;
	}

	return true;
}

bool bgfx_shader::validate_value(const Value& value, const std::string &prefix, const osd_options &options)
{
	if (!validate_parameters(value, prefix))
		return false;

	uint64_t flags = 0;
	std::string vertex_name;
	std::string fragment_name;
	std::vector<std::unique_ptr<bgfx_uniform> > uniforms;

	if (!get_base_shader_data(value, prefix, flags, vertex_name, fragment_name, uniforms))
		return false;

	if (!shaderprog_manager::is_shader_program_present(options, vertex_name))
		return false;

	if (!shaderprog_manager::is_shader_program_present(options, fragment_name))
		return false;

	return true;
}

bool bgfx_shader::validate_parameters(const Value& value, const std::string &prefix)
{
	if (!READER_CHECK(value.HasMember("depth"), "%sMust have object value 'depth' (what are our Z-buffer settings?)\n", prefix)) return false;
	if (!READER_CHECK(value.HasMember("cull"), "%sMust have object value 'cull' (do we cull triangles based on winding?)\n", prefix)) return false;
	if (!READER_CHECK(value.HasMember("write"), "%sMust have object value 'write' (what are our color buffer write settings?)\n", prefix)) return false;
	if (!READER_CHECK(value.HasMember("vertex"), "%sMust have string value 'vertex' (what is our vertex shader?)\n", prefix)) return false;
	if (!READER_CHECK(value["vertex"].IsString(), "%sValue 'vertex' must be a string\n", prefix)) return false;
	if (!READER_CHECK(value.HasMember("fragment") || value.HasMember("pixel"), "%sMust have string value named 'fragment' or 'pixel' (what is our fragment/pixel shader?)\n", prefix)) return false;
	if (!READER_CHECK(!value.HasMember("fragment") || value["fragment"].IsString(), "%sValue 'fragment' must be a string\n", prefix)) return false;
	if (!READER_CHECK(!value.HasMember("pixel") || value["pixel"].IsString(), "%sValue 'pixel' must be a string\n", prefix)) return false;
	if (!READER_CHECK(value.HasMember("uniforms"), "%sMust have array value 'uniforms' (what are our shader's parameters?)\n", prefix)) return false;
	if (!READER_CHECK(value["uniforms"].IsArray(), "%sValue 'uniforms' must be an array\n", prefix)) return false;
	return true;
}

bool bgfx_shader::validate_uniform(const Value& value, const std::string &prefix)
{
	if (!READER_CHECK(value.HasMember("name"), "%sMust have string value 'name' (what is this uniform called in the shader code?)\n", prefix)) return false;
	if (!READER_CHECK(value["name"].IsString(), "%sValue 'name' must be a string\n", prefix)) return false;
	if (!READER_CHECK(value.HasMember("type"), "%sMust have string value 'type' [int, vec4, mat3, mat4]\n", prefix)) return false;
	if (!READER_CHECK(value.HasMember("values"), "%sMust have array value 'values' (what are the uniform's default values?)\n", prefix)) return false;
	if (!READER_CHECK(value["values"].IsArray(), "%sValue 'values' must be an array\n", prefix)) return false;
	return true;
}
