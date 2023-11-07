// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  clear.h - View clear info for a BGFX effect entry
//
//============================================================

#pragma once

#ifndef DRAWBGFX_CLEAR
#define DRAWBGFX_CLEAR

#include "statereader.h"

#include <bgfx/bgfx.h>

class clear_state : public state_reader
{
public:
	clear_state(uint64_t flags, uint32_t color, float depth, uint8_t stencil);

	static clear_state* from_json(const Value& value, const std::string &prefix);

	void bind(int view) const;

private:
	static bool validate_parameters(const Value& value, const std::string &prefix);

	const uint64_t  m_flags;
	const uint32_t  m_color;
	const float     m_depth;
	const uint8_t   m_stencil;

	static const int FLAG_COUNT = 3;
	static const string_to_enum FLAG_NAMES[FLAG_COUNT];
};

#endif // DRAWBGFX_CLEAR
