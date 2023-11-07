// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  shadermanager.cpp - BGFX shader manager
//
//  Maintains a string-to-entry lookup of BGFX shader
//  shaders, defined by shader.h
//
//============================================================

#include "shadermanager.h"

#include "shader.h"
#include "shadermanager.h"

#include "path.h"

#include "osdfile.h"
#include "modules/lib/osdobj_common.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <bx/readerwriter.h>
#include <bx/file.h>

#include <bgfx/bgfx.h>

#include <utility>


static bool prepare_shader_document(const std::string &name, const osd_options &options, rapidjson::Document &document)
{
	std::string full_name = name;
	if (full_name.length() < 5 || (full_name.compare(full_name.length() - 5, 5, ".json") != 0))
	{
		full_name += ".json";
	}

	const std::string path = util::path_concat(options.bgfx_path(), "shaders", full_name);

	bx::FileReader reader;
	if (!bx::open(&reader, path.c_str()))
	{
		osd_printf_error("Unable to open shader file %s\n", path);
		return false;
	}

	const int32_t size = bx::getSize(&reader);
	std::unique_ptr<char []> data(new (std::nothrow) char [size + 1]);
	if (!data)
	{
		osd_printf_error("Out of memory reading shader file %s\n", path);
		bx::close(&reader);
		return false;
	}

	bx::ErrorAssert err;
	bx::read(&reader, reinterpret_cast<void*>(data.get()), size, &err);
	bx::close(&reader);
	data[size] = 0;

	document.Parse<rapidjson::kParseCommentsFlag>(data.get());
	data.reset();

	if (document.HasParseError())
	{
		std::string error(rapidjson::GetParseError_En(document.GetParseError()));
		osd_printf_error("Unable to parse shader %s. Errors returned:\n%s\n", path, error);
		return false;
	}

	return true;
}


// keep constructor and destructor out-of-line so the header works with forward declarations

shader_manager::shader_manager(shaderprog_manager& shaderprogs) : m_shaderprogs(shaderprogs)
{
}

shader_manager::~shader_manager()
{
	// the map will automatically dispose of the shaders
}

bgfx_shader* shader_manager::get_or_load_shader(const osd_options &options, const std::string &name)
{
	const auto iter = m_shaders.find(name);
	if (iter != m_shaders.end())
		return iter->second.get();

	return load_shader(options, name);
}

bgfx_shader* shader_manager::load_shader(const osd_options &options, const std::string &name)
{
	rapidjson::Document document;
	if (!prepare_shader_document(name, options, document))
	{
		return nullptr;
	}

	std::unique_ptr<bgfx_shader> shader = bgfx_shader::from_json(name, document, "Shader '" + name + "': ", options, m_shaderprogs);

	if (!shader)
	{
		osd_printf_error("Unable to load shader %s\n", name);
		return nullptr;
	}

	return m_shaders.emplace(name, std::move(shader)).first->second.get();
}

bool shader_manager::validate_shader(const osd_options &options, const std::string &name)
{
	rapidjson::Document document;
	if (!prepare_shader_document(name, options, document))
	{
		return false;
	}

	return bgfx_shader::validate_value(document, "Shader '" + name + "': ", options);
}
