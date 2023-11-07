// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  shadermanager.h - BGFX shader manager
//
//  Maintains a string-to-entry lookup of BGFX shader
//  effects, defined by shader.h
//
//============================================================

#ifndef MAME_RENDER_BGFX_SHADERMANAGER_H
#define MAME_RENDER_BGFX_SHADERMANAGER_H

#pragma once

#include "statereader.h"

#include <map>
#include <memory>
#include <string>

class bgfx_shader;
class osd_options;
class shaderprog_manager;

class shader_manager : public state_reader
{
public:
	shader_manager(shaderprog_manager& shaderprogs);
	~shader_manager();

	// Getters
	bgfx_shader* get_or_load_shader(const osd_options &options, const std::string &name);
	static bool validate_shader(const osd_options &options, const std::string &name);

private:
	bgfx_shader* load_shader(const osd_options &options, const std::string &name);

	shaderprog_manager &m_shaderprogs;
	std::map<std::string, std::unique_ptr<bgfx_shader> > m_shaders;
};

#endif // MAME_RENDER_BGFX_SHADERMANAGER_H
