// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  shaderprogmanager.cpp - BGFX shader program manager
//
//  Maintains a mapping between strings and BGFX shaders,
//  either vertex or pixel/fragment.
//
//============================================================

#include "shaderprogmanager.h"

#include "emucore.h"

#include "osdfile.h"
#include "modules/lib/osdobj_common.h"

#include <bx/file.h>
#include <bx/math.h>
#include <bx/readerwriter.h>

shaderprog_manager::~shaderprog_manager()
{
	for (std::pair<std::string, bgfx::ShaderHandle> shaderprog : m_shader_programs)
	{
		bgfx::destroy(shaderprog.second);
	}
	m_shader_programs.clear();
}

bgfx::ShaderHandle shaderprog_manager::get_or_load_shader_program(const osd_options &options, const std::string &name)
{
	std::map<std::string, bgfx::ShaderHandle>::iterator iter = m_shader_programs.find(name);
	if (iter != m_shader_programs.end())
	{
		return iter->second;
	}

	bgfx::ShaderHandle handle = load_shader_program(options, name);
	if (handle.idx != bgfx::kInvalidHandle)
	{
		m_shader_programs[name] = handle;
	}

	return handle;
}

bgfx::ShaderHandle shaderprog_manager::load_shader_program(const osd_options &options, const std::string &name)
{
	std::string shaderprog_path = make_path_string(options, name);
	const bgfx::Memory* mem = load_mem(shaderprog_path + name + ".bin");
	if (mem != nullptr)
	{
		return bgfx::createShader(mem);
	}

	return BGFX_INVALID_HANDLE;
}

bool shaderprog_manager::is_shader_program_present(const osd_options &options, const std::string &name)
{
	std::string shaderprog_path = make_path_string(options, name);
	std::string file_name = shaderprog_path + name + ".bin";
	bx::FileReader reader;
	bx::ErrorAssert err;
	if (bx::open(&reader, file_name.c_str()))
	{
		uint32_t expected_size(bx::getSize(&reader));
		uint8_t *data = new uint8_t[expected_size];
		uint32_t read_size = (uint32_t)bx::read(&reader, data, expected_size, &err);
		delete [] data;
		bx::close(&reader);

		return expected_size == read_size;
	}

	return false;
}

std::string shaderprog_manager::make_path_string(const osd_options &options, const std::string &name)
{
	std::string shaderprog_path(options.bgfx_path());
	shaderprog_path += PATH_SEPARATOR "progs" PATH_SEPARATOR;
	switch (bgfx::getRendererType())
	{
		case bgfx::RendererType::Noop:
		case bgfx::RendererType::Direct3D9:
			shaderprog_path += "dx9";
			break;

		case bgfx::RendererType::Direct3D11:
		case bgfx::RendererType::Direct3D12:
			shaderprog_path += "dx11";
			break;

		case bgfx::RendererType::Gnm:
			shaderprog_path += "pssl";
			break;

		case bgfx::RendererType::Metal:
			shaderprog_path += "metal";
			break;

		case bgfx::RendererType::OpenGL:
			shaderprog_path += "glsl";
			break;

		case bgfx::RendererType::OpenGLES:
			shaderprog_path += "essl";
			break;

		case bgfx::RendererType::Vulkan:
			shaderprog_path += "spirv";
			break;

		default:
			fatalerror("Unknown BGFX renderer type %d", bgfx::getRendererType());
	}
	shaderprog_path += PATH_SEPARATOR;
	return shaderprog_path;
}

const bgfx::Memory* shaderprog_manager::load_mem(const std::string &name)
{
	bx::FileReader reader;
	if (bx::open(&reader, name.c_str()))
	{
		bx::ErrorAssert err;
		uint32_t size(bx::getSize(&reader));
		const bgfx::Memory* mem = bgfx::alloc(size + 1);
		bx::read(&reader, mem->data, size, &err);
		bx::close(&reader);

		mem->data[mem->size - 1] = '\0';
		return mem;
	}
	else
	{
		osd_printf_error("Unable to load shader program %s\n", name);
	}
	return nullptr;
}
