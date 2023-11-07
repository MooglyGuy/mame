// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  target.cpp - Render target abstraction for BGFX layer
//
//============================================================

#include "emucore.h"

#include "target.h"

#include "bgfxutil.h"
#include "effectmanager.h"

const bgfx_target::string_to_enum bgfx_target::STYLE_NAMES[bgfx_target::STYLE_COUNT] = {
	{ "guest",  TARGET_STYLE_GUEST },
	{ "native", TARGET_STYLE_NATIVE },
	{ "custom", TARGET_STYLE_CUSTOM }
};

bgfx_target::bgfx_target(std::string name, bgfx::TextureFormat::Enum format, uint16_t width, uint16_t height, uint16_t xprescale, uint16_t yprescale,
	uint32_t style, bool double_buffer, bool filter, uint16_t scale, uint32_t screen)
	: m_name(name)
	, m_format(format)
	, m_targets(nullptr)
	, m_textures(nullptr)
	, m_width(width)
	, m_height(height)
	, m_xprescale(xprescale)
	, m_yprescale(yprescale)
	, m_double_buffer(double_buffer)
	, m_style(style)
	, m_filter(filter)
	, m_scale(scale)
	, m_screen(screen)
	, m_current_page(0)
	, m_initialized(false)
	, m_page_count(double_buffer ? 2 : 1)
{
	if (m_width > 0 && m_height > 0)
	{
		m_width *= m_scale;
		m_height *= m_scale;

		uint32_t wrap_mode = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
		uint32_t filter_mode = filter ? (BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC) : (BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT);
		uint32_t depth_flags = wrap_mode | (BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT);

		m_textures = new bgfx::TextureHandle[m_page_count * 2];
		m_targets = new bgfx::FrameBufferHandle[m_page_count];
		for (int page = 0; page < m_page_count; page++)
		{
			m_textures[page] = bgfx::createTexture2D(m_width * xprescale, m_height * yprescale, false, 1, format, wrap_mode | filter_mode | BGFX_TEXTURE_RT);
			assert(m_textures[page].idx != 0xffff);

			m_textures[m_page_count + page] = bgfx::createTexture2D(m_width * xprescale, m_height * yprescale, false, 1, bgfx::TextureFormat::D32F, depth_flags | BGFX_TEXTURE_RT);
			assert(m_textures[m_page_count + page].idx != 0xffff);

			bgfx::TextureHandle handles[2] = { m_textures[page], m_textures[m_page_count + page] };
			m_targets[page] = bgfx::createFrameBuffer(2, handles, false);

			assert(m_targets[page].idx != 0xffff);
		}

		m_initialized = true;
	}
}

bgfx_target::bgfx_target(void *handle, uint16_t width, uint16_t height)
	: m_name("backbuffer")
	, m_format(bgfx::TextureFormat::Unknown)
	, m_targets(nullptr)
	, m_textures(nullptr)
	, m_width(width)
	, m_height(height)
	, m_xprescale(1)
	, m_yprescale(1)
	, m_double_buffer(false)
	, m_style(TARGET_STYLE_CUSTOM)
	, m_filter(false)
	, m_scale(0)
	, m_screen(-1)
	, m_current_page(0)
	, m_initialized(true)
	, m_page_count(0)
{
	m_targets = new bgfx::FrameBufferHandle[1];
	m_targets[0] = bgfx::createFrameBuffer(handle, width, height, bgfx::TextureFormat::Count, bgfx::TextureFormat::D32F);

	// No backing texture
}

bgfx_target::~bgfx_target()
{
	if (!m_initialized)
	{
		return;
	}

	if (m_page_count > 0)
	{
		for (int page = 0; page < m_page_count; page++)
		{
			bgfx::destroy(m_targets[page]);
			bgfx::destroy(m_textures[m_page_count + page]);
			bgfx::destroy(m_textures[page]);
		}
		delete [] m_textures;
		delete [] m_targets;
	}
	else
	{
		bgfx::destroy(m_targets[0]);
		delete [] m_targets;
	}
}

bgfx_target* bgfx_target::from_json(const Value& value, const std::string &prefix, effect_manager& effects,
		uint32_t screen_index, uint16_t user_prescale, uint16_t max_prescale_size)
{
	if (!validate_parameters(value, prefix))
	{
		return nullptr;
	}

	std::string target_name = value["name"].GetString();
	uint32_t mode = uint32_t(get_enum_from_value(value, "mode", TARGET_STYLE_NATIVE, STYLE_NAMES, STYLE_COUNT));
	bool bilinear = get_bool(value, "bilinear", true);
	bool double_buffer = get_bool(value, "doublebuffer", true);
	int scale = 1;
	if (value.HasMember("scale"))
	{
		scale = int(floor(value["scale"].GetDouble() + 0.5));
	}
	bool use_user_prescale = get_bool(value, "user_prescale", false);

	uint16_t width = 0;
	uint16_t height = 0;
	uint16_t xprescale = 1;
	uint16_t yprescale = 1;
	switch (mode)
	{
		case TARGET_STYLE_GUEST:
		{
			width = effects.targets().width(TARGET_STYLE_GUEST, screen_index);
			height = effects.targets().height(TARGET_STYLE_GUEST, screen_index);

			if (use_user_prescale)
			{
				xprescale = user_prescale;
				yprescale = user_prescale;
				bgfx_util::find_prescale_factor(width, height, max_prescale_size, xprescale, yprescale);
			}
			break;
		}
		case TARGET_STYLE_NATIVE:
			width = effects.targets().width(TARGET_STYLE_NATIVE, screen_index);
			height = effects.targets().height(TARGET_STYLE_NATIVE, screen_index);

			if (use_user_prescale)
			{
				xprescale = user_prescale;
				yprescale = user_prescale;
				bgfx_util::find_prescale_factor(width, height, max_prescale_size, xprescale, yprescale);
			}
			break;
		case TARGET_STYLE_CUSTOM:
			READER_WARN(!value.HasMember("user_prescale"), "%sTarget '%s': user_prescale parameter is not used for custom-type render targets.\n", prefix, target_name);
			if (!READER_CHECK(value.HasMember("width"), "%sTarget '%s': Must have numeric value 'width'\n", prefix, target_name)) return nullptr;
			if (!READER_CHECK(value["width"].IsNumber(), "%sTarget '%s': Value 'width' must be a number\n", prefix, target_name)) return nullptr;
			if (!READER_CHECK(value.HasMember("height"), "%sTarget '%s': Must have numeric value 'height'\n", prefix, target_name)) return nullptr;
			if (!READER_CHECK(value["height"].IsNumber(), "%sTarget '%s': Value 'height' must be a number\n", prefix, target_name)) return nullptr;
			width = uint16_t(value["width"].GetDouble());
			height = uint16_t(value["height"].GetDouble());
			break;
	}

	return effects.targets().create_target(std::move(target_name), bgfx::TextureFormat::BGRA8, width, height, xprescale, yprescale, mode, double_buffer, bilinear, scale, screen_index);
}

void bgfx_target::page_flip()
{
	if (!m_initialized) return;

	if (m_double_buffer)
	{
		m_current_page = 1 - m_current_page;
	}
}

bgfx::FrameBufferHandle bgfx_target::target()
{
	if (!m_initialized) return BGFX_INVALID_HANDLE;
	return m_targets[m_current_page];
}

bgfx::TextureHandle bgfx_target::texture() const
{
	if (!m_initialized) return BGFX_INVALID_HANDLE;

	if (m_double_buffer)
	{
		return m_textures[1 - m_current_page];
	}
	else
	{
		return m_textures[m_current_page];
	}
}

bool bgfx_target::validate_parameters(const Value& value, const std::string &prefix)
{
	if (!READER_CHECK(value.HasMember("name"), "%sMust have string value 'name'\n", prefix)) return false;
	if (!READER_CHECK(value["name"].IsString(), "%sValue 'name' must be a string\n", prefix)) return false;
	if (!READER_CHECK(value.HasMember("mode"), "%sMust have string enum 'mode'\n", prefix)) return false;
	if (!READER_CHECK(value["mode"].IsString(), "%sValue 'mode' must be a string (what screens does this apply to?)\n", prefix)) return false;
	if (!READER_CHECK(!value.HasMember("bilinear") || value["bilinear"].IsBool(), "%sValue 'bilinear' must be a boolean\n", prefix)) return false;
	if (!READER_CHECK(!value.HasMember("doublebuffer") || value["doublebuffer"].IsBool(), "%sValue 'doublebuffer' must be a boolean\n", prefix)) return false;
	if (!READER_CHECK(!value.HasMember("user_prescale") || value["user_prescale"].IsBool(), "%sValue 'user_prescale' must be a boolean\n", prefix)) return false;
	if (!READER_CHECK(!value.HasMember("scale") || value["scale"].IsNumber(), "%sValue 'scale' must be a numeric value\n", prefix)) return false;
	return true;
}
