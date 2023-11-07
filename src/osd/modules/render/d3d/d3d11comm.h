// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  d3d11comm.h - Common Win32 Direct3D11 structures
//
//============================================================

#ifndef MAME_RENDER_D3D_D3D11COMM_H
#define MAME_RENDER_D3D_D3D11COMM_H

#pragma once

// lib/util
#include "bitmap.h"

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstring>
#include <memory>
#include <vector>


//============================================================
//  CONSTANTS
//============================================================

#define MAX_BLOOM_COUNT 15 // shader model 3.0 support up to 16 samplers, but we need the last for the original texture
#define HALF_BLOOM_COUNT 8

//============================================================
//  FORWARD DECLARATIONS
//============================================================

class d3d11_texture_info;
class d3d11_texture_manager;
class renderer_d3d11;

//============================================================
//  TYPE DEFINITIONS
//============================================================

class d3d11_vec2f
{
public:
	d3d11_vec2f()
	{
		memset(&c, 0, sizeof(float) * 2);
	}
	d3d11_vec2f(float x, float y)
	{
		c.x = x;
		c.y = y;
	}

	d3d11_vec2f operator+(const d3d11_vec2f& a) const
	{
		return d3d11_vec2f(c.x + a.c.x, c.y + a.c.y);
	}

	d3d11_vec2f operator-(const d3d11_vec2f& a) const
	{
		return d3d11_vec2f(c.x - a.c.x, c.y - a.c.y);
	}

	struct
	{
		float x, y;
	} c;
};

class d3d11_texture_manager
{
public:
	d3d11_texture_manager(renderer_d3d11 &d3d);
	~d3d11_texture_manager();

	void                    update_textures();

	void                    create_resources();
	void                    delete_resources();

	d3d11_texture_info *    find_texinfo(const render_texinfo *texture, uint32_t flags);
	uint32_t                texture_compute_hash(const render_texinfo *texture, uint32_t flags);

	DXGI_FORMAT             get_yuv_format() const { return m_yuv_format; }

	DWORD                   get_texture_caps() const { return m_texture_caps; }
	DWORD                   get_max_texture_aspect() const { return m_texture_max_aspect; }
	DWORD                   get_max_texture_width() const { return m_texture_max_width; }
	DWORD                   get_max_texture_height() const { return m_texture_max_height; }

	d3d11_texture_info *    get_default_texture() const { return m_default_texture; }

	renderer_d3d11 &        get_renderer() const { return m_renderer; }

	std::vector<std::unique_ptr<d3d11_texture_info>> m_texture_list;  // list of active textures

private:
	renderer_d3d11 &        m_renderer;
	DXGI_FORMAT             m_yuv_format;               // format to use for YUV textures

	DWORD                   m_texture_caps;             // textureCaps field
	DWORD                   m_texture_max_aspect;       // texture maximum aspect ratio
	DWORD                   m_texture_max_width;        // texture maximum width
	DWORD                   m_texture_max_height;       // texture maximum height

	bitmap_rgb32            m_default_bitmap;           // experimental: default bitmap
	d3d11_texture_info *    m_default_texture;          // experimental: default texture
};


/* d3d11_texture_info holds information about a texture */
class d3d11_texture_info
{
public:
	d3d11_texture_info(d3d11_texture_manager &manager, const render_texinfo *texsource, int prescale, uint32_t flags);
	~d3d11_texture_info();

	render_texinfo &        get_texinfo() { return m_texinfo; }

	int                     get_width() const { return m_rawdims.c.x; }
	int                     get_height() const { return m_rawdims.c.y; }
	int                     get_xscale() const { return m_xprescale; }
	int                     get_yscale() const { return m_yprescale; }

	uint32_t                get_flags() const { return m_flags; }

	void                    set_data(const render_texinfo *texsource, uint32_t flags);

	uint32_t                get_hash() const { return m_hash; }

	void                    increment_frame_count() { m_cur_frame++; }
	void                    mask_frame_count(int mask) { m_cur_frame %= mask; }

	int                     get_cur_frame() const { return m_cur_frame; }

	ID3D11ShaderResourceView *get_prescaled_view() const { return m_prescaled_view; }
	ID3D11ShaderResourceView * const* get_view() const { return &m_view; }

	d3d11_vec2f &           get_uvstart() { return m_start; }
	d3d11_vec2f &           get_uvstop() { return m_stop; }
	d3d11_vec2f &           get_rawdims() { return m_rawdims; }

private:
	void prescale();

	inline void copyline_palette16(uint32_t *dst, const uint16_t *src, int width, const rgb_t *palette);
	inline void copyline_rgb32(uint32_t *dst, const uint32_t *src, int width, const rgb_t *palette);
	inline void copyline_argb32(uint32_t *dst, const uint32_t *src, int width, const rgb_t *palette);
	inline void copyline_yuy16_to_yuy2(uint16_t *dst, const uint16_t *src, int width, const rgb_t *palette);

	d3d11_texture_manager & m_texture_manager;          // texture manager pointer

	renderer_d3d11 &        m_renderer;                 // renderer pointer

	const uint32_t          m_hash;                     // hash value for the texture
	const uint32_t          m_flags;                    // rendering flags
	render_texinfo          m_texinfo;                  // copy of the texture info
	d3d11_vec2f             m_start;                    // beggining UV coordinates
	d3d11_vec2f             m_stop;                     // ending UV coordinates
	d3d11_vec2f             m_rawdims;                  // raw dims of the texture
	int                     m_xprescale, m_yprescale;   // X/Y prescale factor
	int                     m_cur_frame;                // what is our current frame?
	ID3D11Texture2D        *m_tex;
	ID3D11ShaderResourceView *m_view;                 // D3D11 shader-resource view pointer for this texture (no prescaling)

	D3D11_TEXTURE2D_DESC    m_prescaled_desc;
	D3D11_SUBRESOURCE_DATA  m_prescaled_data;
	ID3D11Texture2D        *m_prescaled_tex;
	ID3D11ShaderResourceView *m_prescaled_view;
};

/* d3d11_poly_info holds information about a single polygon/d3d primitive */
class d3d11_poly_info
{
public:
	void init(D3D_PRIMITIVE_TOPOLOGY type, uint32_t count, uint32_t numindices, uint32_t numverts, uint32_t flags,
		d3d11_texture_info *texture, float prim_width, float prim_height, uint32_t tint)
	{
		m_type = type;
		m_count = count;
		m_numindices = numindices;
		m_numverts = numverts;
		m_flags = flags;
		m_texture = texture;
		m_prim_width = prim_width;
		m_prim_height = prim_height;
		m_tint = tint;
	}

	D3D_PRIMITIVE_TOPOLOGY  type() const { return m_type; }
	uint32_t                count() const { return m_count; }
	uint32_t                numindices() const { return m_numindices; }
	uint32_t                numverts() const { return m_numverts; }
	uint32_t                flags() const { return m_flags; }

	d3d11_texture_info *    texture() const { return m_texture; }

	float                   prim_width() const { return m_prim_width; }
	float                   prim_height() const { return m_prim_height; }

	DWORD                   tint() const { return m_tint; }

private:
	D3D_PRIMITIVE_TOPOLOGY  m_type;         // type of primitive
	uint32_t                m_count;        // total number of primitives
	uint32_t                m_numindices;   // total number of indices
	uint32_t                m_numverts;     // total number of vertices
	uint32_t                m_flags;        // rendering flags

	d3d11_texture_info *    m_texture;      // pointer to texture info

	float                   m_prim_width;   // used by quads
	float                   m_prim_height;  // used by quads

	uint32_t                m_tint;         // color tint for primitive
};

/* d3d11_vertex describes a single vertex */
struct d3d11_vertex
{
	float       x, y, z;                    // X,Y,Z coordinates
	uint8_t     b;                          // diffuse color, blue
	uint8_t     g;                          // diffuse color, green
	uint8_t     r;                          // diffuse color, red
	uint8_t     a;                          // diffuse color, alpha
	float       u0, v0;                     // texture stage 0 coordinates
	float       u1, v1;                     // additional info for vector data
};


/* d3d11_render_target is the information about a Direct3D render target chain */
class d3d11_render_target
{
public:
	// construction/destruction
	d3d11_render_target()
		: target_width(0)
		, target_height(0)
		, width(0)
		, height(0)
		, screen_index(0)
		, bloom_count(0)
	{
	}

	~d3d11_render_target();

	bool init(renderer_d3d11 *d3d, int source_width, int source_height, int target_width, int target_height, int screen_index);

	// real target dimension
	int target_width;
	int target_height;

	// only used to identify/find the render target
	int width;
	int height;

	int screen_index;

	ID3D11Texture2D *source_texture[2];
	ID3D11RenderTargetView *source_rt_view[2];
	ID3D11ShaderResourceView *source_res_view[2];
	ID3D11Texture2D *source_depth_texture[2];
	ID3D11DepthStencilView *source_depth_rt_view[2];
	D3D11_VIEWPORT source_viewport;

	ID3D11Texture2D *target_texture[2];
	ID3D11RenderTargetView *target_rt_view[2];
	ID3D11ShaderResourceView *target_res_view[2];
	ID3D11Texture2D *target_depth_texture[2];
	ID3D11DepthStencilView *target_depth_rt_view[2];
	D3D11_VIEWPORT target_viewport;

	ID3D11Texture2D *cache_texture;
	ID3D11RenderTargetView *cache_rt_view;
	ID3D11ShaderResourceView *cache_res_view;
	ID3D11Texture2D *cache_depth_texture;
	ID3D11DepthStencilView *cache_depth_rt_view;
	D3D11_VIEWPORT cache_viewport;

	ID3D11Texture2D *bloom_texture[MAX_BLOOM_COUNT];
	ID3D11RenderTargetView *bloom_rt_view[MAX_BLOOM_COUNT];
	ID3D11ShaderResourceView *bloom_res_view[MAX_BLOOM_COUNT];
	ID3D11Texture2D *bloom_depth_texture[MAX_BLOOM_COUNT];
	ID3D11DepthStencilView *bloom_depth_rt_view[MAX_BLOOM_COUNT];
	D3D11_VIEWPORT bloom_viewport[MAX_BLOOM_COUNT];

	float bloom_dims[MAX_BLOOM_COUNT][2];
	float source_dims[2];

	int bloom_count;
};

#endif // MAME_RENDER_D3D_D3D11COMM_H
