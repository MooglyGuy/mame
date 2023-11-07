// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  shaderprogmanager.h - BGFX shader program manager
//
//  Maintains a mapping between strings and BGFX shaders,
//  either vertex or pixel/fragment.
//
//============================================================

#ifndef MAME_RENDER_BGFX_SHADERPROGMANAGER_H
#define MAME_RENDER_BGFX_SHADERPROGMANAGER_H

#pragma once

#include <bgfx/bgfx.h>

#include <map>
#include <string>


class osd_options;


class shaderprog_manager
{
public:
	shaderprog_manager() { }
	~shaderprog_manager();

	// Getters
	bgfx::ShaderHandle get_or_load_shader_program(const osd_options &options, const std::string &name);
	static bgfx::ShaderHandle load_shader_program(const osd_options &options, const std::string &name);
	static bool is_shader_program_present(const osd_options &options, const std::string &name);

private:
	static std::string make_path_string(const osd_options &options, const std::string &name);
	static const bgfx::Memory* load_mem(const std::string &name);

	std::map<std::string, bgfx::ShaderHandle> m_shader_programs;
};

#endif // MAME_RENDER_BGFX_SHADERPROGRAMMANAGER_H
