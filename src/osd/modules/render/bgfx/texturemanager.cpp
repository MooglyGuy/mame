// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  texturemanager.cpp - BGFX texture manager
//
//  Maintains a string-to-entry mapping for any registered
//  textures.
//
//============================================================

#include "texturemanager.h"

#include "bgfxutil.h"
#include "texture.h"

#include "modules/render/copyutil.h"
#include "osdcore.h"

#include "emucore.h"
#include "fileio.h"
#include "render.h"
#include "rendutil.h"

texture_manager::texture_manager()
{
	// out-of-line so header works with forward declarations
}

texture_manager::~texture_manager()
{
	m_textures.clear();

	for (sequenced_handle & mame_texture : m_mame_textures)
	{
		bgfx::destroy(mame_texture.handle);
	}
	m_mame_textures.clear();
}

void texture_manager::add_provider(const std::string &name, std::unique_ptr<bgfx_texture_handle_provider> &&provider)
{
	const auto iter = m_textures.find(name);
	if (iter != m_textures.end())
		iter->second = std::make_pair(provider.get(), std::move(provider));
	else
		m_textures.emplace(name, std::make_pair(provider.get(), std::move(provider)));
}

void texture_manager::add_provider(const std::string &name, bgfx_texture_handle_provider &provider)
{
	const auto iter = m_textures.find(name);
	if (iter != m_textures.end())
		iter->second = std::make_pair(&provider, nullptr);
	else
		m_textures.emplace(name, std::make_pair(&provider, nullptr));
}

bgfx_texture* texture_manager::create_texture(
		const std::string &name,
		bgfx::TextureFormat::Enum format,
		uint32_t width,
		uint32_t width_margin,
		uint32_t height,
		void* data,
		uint32_t flags)
{
	auto texture = std::make_unique<bgfx_texture>(name, format, width, width_margin, height, flags, data);
	bgfx_texture &result = *texture;
	m_textures[name] = std::make_pair(texture.get(), std::move(texture));
	return &result;
}

bgfx_texture* texture_manager::create_png_texture(
		const std::string &path,
		const std::string &file_name,
		std::string texture_name,
		uint32_t flags,
		uint32_t screen)
{
	bitmap_argb32 bitmap;
	emu_file file(path, OPEN_FLAG_READ);
	if (!file.open(file_name))
	{
		render_load_png(bitmap, file);
		file.close();
	}

	if (bitmap.width() == 0 || bitmap.height() == 0)
	{
		osd_printf_error("Unable to load PNG '%s' from path '%s'\n", file_name, path);
		return nullptr;
	}

	const uint32_t width = bitmap.width();
	const uint32_t height = bitmap.height();
	auto data32 = std::make_unique<uint32_t []>(width * height);

	const uint32_t rowpixels = bitmap.rowpixels();
	auto* base = reinterpret_cast<uint32_t *>(bitmap.raw_pixptr(0));
	for (int y = 0; y < height; y++)
	{
		copy_util::copyline_argb32_to_bgra(&data32[y * width], base + y * rowpixels, width, nullptr);
	}

	if (screen >= 0)
	{
		texture_name += std::to_string(screen);
	}
	return create_texture(texture_name, bgfx::TextureFormat::BGRA8, width, 0, height, data32.get(), flags);
}

uint32_t texture_manager::texture_compute_hash(void *texture_base, uint32_t flags)
{
	return (uintptr_t)texture_base ^ (flags & (PRIMFLAG_BLENDMODE_MASK | PRIMFLAG_TEXFORMAT_MASK));
}

bool texture_manager::find_mame_texture(uint32_t hash, void *base, int width, int height, uint32_t flags, uint64_t key, sequenced_handle & out_mame_handle)
{
	// find a match
	for (auto it = m_mame_textures.begin(); it != m_mame_textures.end(); it++)
	{
		const uint32_t test_screen = uint32_t(it->key >> 57);
		const uint32_t test_page = uint32_t(it->key >> 56) & 1;
		const uint32_t prim_screen = uint32_t(key >> 57);
		const uint32_t prim_page = uint32_t(key >> 56) & 1;
		if (test_screen != prim_screen || test_page != prim_page)
			continue;

		if (it->hash == hash && it->base == base && it->width == width && it->height == height &&
			((it->flags ^ flags) & (PRIMFLAG_BLENDMODE_MASK | PRIMFLAG_TEXFORMAT_MASK)) == 0)
		{
			out_mame_handle = *it;
			return true;
		}
	}

	return false;
}

bgfx::TextureHandle texture_manager::create_or_update_mame_texture(
		uint32_t format,
		int width,
		int width_margin,
		int height,
		int rowpixels,
		const rgb_t *palette,
		void *base,
		uint32_t seqid,
		uint32_t flags,
		uint64_t key,
		uint64_t old_key)
{
	bgfx::TextureFormat::Enum dst_format = bgfx::TextureFormat::BGRA8;
	uint16_t pitch = width;
	int width_div_factor = 1;
	int width_mul_factor = 1;

	// First see if we can find an existing, matching, texture to return
	const uint32_t hash = texture_compute_hash(base, flags);
	sequenced_handle mame_handle;
	if (find_mame_texture(hash, base, width, height, flags, key, mame_handle))
	{
		bgfx::TextureHandle handle = mame_handle.handle;
		// if there's an existing texture, but with a different seqid, copy the data
		if (mame_handle.seqid != seqid)
		{
			const bgfx::Memory* mem = bgfx_util::mame_texture_data_to_bgfx_texture_data(dst_format, format, rowpixels, width_margin, height, palette, base, pitch, width_div_factor, width_mul_factor);
			bgfx::updateTexture2D(mame_handle.handle, 0, 0, 0, 0, uint16_t((rowpixels * width_mul_factor) / width_div_factor), uint16_t(height), mem, pitch);
		}
		return handle;
	}

	const bgfx::Memory* mem = bgfx_util::mame_texture_data_to_bgfx_texture_data(dst_format, format, rowpixels, width_margin, height, palette, base, pitch, width_div_factor, width_mul_factor);
	const uint16_t adjusted_width = uint16_t((rowpixels * width_mul_factor) / width_div_factor);
	bgfx::TextureHandle bgfx_handle = bgfx::createTexture2D(adjusted_width, height, false, 1, dst_format, flags, nullptr);
	bgfx::updateTexture2D(bgfx_handle, 0, 0, 0, 0, adjusted_width, uint16_t(height), mem, pitch);

	mame_handle = { bgfx_handle, hash, base, width, height, flags, key, seqid };
	m_mame_textures.push_back(mame_handle);
	return bgfx_handle;
}

bgfx::TextureHandle texture_manager::handle(const std::string &name)
{
	bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
	const auto iter = m_textures.find(name);
	if (iter != m_textures.end())
		handle = iter->second.first->texture();

	assert(handle.idx != bgfx::kInvalidHandle);
	return handle;
}

bgfx_texture_handle_provider* texture_manager::provider(const std::string &name)
{
	const auto iter = m_textures.find(name);
	if (iter != m_textures.end())
		return iter->second.first;
	else
		return nullptr;
}

void texture_manager::remove_provider(const std::string &name)
{
	m_textures.erase(name);
}
