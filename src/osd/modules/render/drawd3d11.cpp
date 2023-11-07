// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//============================================================
//
//  drawd3d.cpp - Win32 Direct3D implementation
//
//============================================================

// MAME headers
#include "emu.h"
#include "emuopts.h"
#include "render.h"
#include "rendutil.h"
#include "screen.h"

// MAMEOS headers
#include "drawd3d11.h"
#include "d3d/d3d11hlsl.h"
#include "render_module.h"

// from OSD implementation
#include "strconv.h"
#include "window.h"
#include "winmain.h"

// general OSD headers
#include "modules/monitor/monitor_module.h"

// lib/util
#include "aviio.h"

#include <utility>
#include <cfloat>


//============================================================
//  OSD MODULE
//============================================================

namespace osd {

namespace {

class video_d3d11 : public osd_module, public render_module
{
public:
	video_d3d11()
		: osd_module(OSD_RENDERER_PROVIDER, "d3d11")
		, m_options(nullptr)
	{
	}

	virtual bool probe() override;
	virtual int init(osd_interface &osd, osd_options const &options) override;
	virtual void exit() override;

	virtual std::unique_ptr<osd_renderer> create(osd_window &window) override;

protected:
	virtual unsigned flags() const override { return FLAG_INTERACTIVE; }

private:
	using d3d11_create_fn = HRESULT (WINAPI *)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
	using d3d_compile_fn = HRESULT (WINAPI *)(LPCWSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

	dynamic_module::ptr m_d3d11_dll;
	dynamic_module::ptr m_d3dcompiler_dll;
	osd_options const *m_options;
};


//============================================================
//  video_d3d11::probe
//============================================================

bool video_d3d11::probe()
{
	// do a dry run of loading the Direct3D11 DLL
	if (dynamic_module::open({ "d3d11.dll" })->bind<d3d11_create_fn>("D3D11CreateDevice") == nullptr)
		return false;

	if (dynamic_module::open({ "D3DCompiler_47.dll" })->bind<d3d_compile_fn>("D3DCompileFromFile") == nullptr)
		return false;

	return true;
}


//============================================================
//  video_d3d11::init
//============================================================

int video_d3d11::init(osd_interface &osd, osd_options const &options)
{
	m_options = &options;

	m_d3d11_dll = dynamic_module::open({ "d3d11.dll" });
	auto const d3d11_create_ptr = m_d3d11_dll->bind<d3d11_create_fn>("D3D11CreateDevice");
	if (!d3d11_create_ptr)
	{
		osd_printf_warning("Direct3D11: Could not find D3D11CreateDevice function in d3d11.dll\n");
		m_d3d11_dll.reset();
		m_options = nullptr;
		return -1;
	}

	m_d3dcompiler_dll = dynamic_module::open({ "D3DCompiler_47.dll" });
	auto const d3d_compile_ptr = m_d3dcompiler_dll->bind<d3d_compile_fn>("D3DCompileFromFile");
	if (!d3d_compile_ptr)
	{
		osd_printf_warning("Direct3D11: Could not find D3DCompileFromFile function in D3DCompiler_47.dll\n");
		m_d3dcompiler_dll.reset();
		m_d3d11_dll.reset();
		m_options = nullptr;
		return -1;
	}

	osd_printf_verbose("Direct3D11: Using D3D11\n");

	return 0;
}


//============================================================
//  video_d3d11::exit
//============================================================

void video_d3d11::exit()
{
	m_d3d11_dll.reset();
	m_d3dcompiler_dll.reset();
	m_options = nullptr;
}


//============================================================
//  video_d3d11::create
//============================================================

std::unique_ptr<osd_renderer> video_d3d11::create(osd_window &window)
{
	return std::make_unique<renderer_d3d11>(window, m_d3d11_dll->bind<d3d11_create_fn>("D3D11CreateDevice"), m_d3dcompiler_dll->bind<d3d_compile_fn>("D3DCompileFromFile"));
}

} // anonymous namespace

} // namespace osd

MODULE_DEFINITION(RENDERER_D3D11, osd::video_d3d11)



//============================================================
//  INLINES
//============================================================

static inline BOOL GetClientRectExceptMenu(HWND hWnd, PRECT pRect, BOOL fullscreen)
{
	static HMENU last_menu;
	static RECT last_rect;
	static RECT cached_rect;
	HMENU menu = GetMenu(hWnd);
	BOOL result = GetClientRect(hWnd, pRect);

	if (!fullscreen || !menu)
		return result;

	// to avoid flicker use cache if we can use
	if (last_menu != menu || memcmp(&last_rect, pRect, sizeof *pRect) != 0)
	{
		last_menu = menu;
		last_rect = *pRect;

		SetMenu(hWnd, nullptr);
		result = GetClientRect(hWnd, &cached_rect);
		SetMenu(hWnd, menu);
	}

	*pRect = cached_rect;
	return result;
}


static inline uint32_t ycc_to_rgb(uint8_t y, uint8_t cb, uint8_t cr)
{
	/* original equations:

	    C = Y - 16
	    D = Cb - 128
	    E = Cr - 128

	    R = clip(( 298 * C           + 409 * E + 128) >> 8)
	    G = clip(( 298 * C - 100 * D - 208 * E + 128) >> 8)
	    B = clip(( 298 * C + 516 * D           + 128) >> 8)

	    R = clip(( 298 * (Y - 16)                    + 409 * (Cr - 128) + 128) >> 8)
	    G = clip(( 298 * (Y - 16) - 100 * (Cb - 128) - 208 * (Cr - 128) + 128) >> 8)
	    B = clip(( 298 * (Y - 16) + 516 * (Cb - 128)                    + 128) >> 8)

	    R = clip(( 298 * Y - 298 * 16                        + 409 * Cr - 409 * 128 + 128) >> 8)
	    G = clip(( 298 * Y - 298 * 16 - 100 * Cb + 100 * 128 - 208 * Cr + 208 * 128 + 128) >> 8)
	    B = clip(( 298 * Y - 298 * 16 + 516 * Cb - 516 * 128                        + 128) >> 8)

	    R = clip(( 298 * Y - 298 * 16                        + 409 * Cr - 409 * 128 + 128) >> 8)
	    G = clip(( 298 * Y - 298 * 16 - 100 * Cb + 100 * 128 - 208 * Cr + 208 * 128 + 128) >> 8)
	    B = clip(( 298 * Y - 298 * 16 + 516 * Cb - 516 * 128                        + 128) >> 8)
	*/
	int r, g, b, common;

	common = 298 * y - 298 * 16;
	r = (common +                        409 * cr - 409 * 128 + 128) >> 8;
	g = (common - 100 * cb + 100 * 128 - 208 * cr + 208 * 128 + 128) >> 8;
	b = (common + 516 * cb - 516 * 128                        + 128) >> 8;

	if (r < 0) r = 0;
	else if (r > 255) r = 255;
	if (g < 0) g = 0;
	else if (g > 255) g = 255;
	if (b < 0) b = 0;
	else if (b > 255) b = 255;

	return rgb_t(0xff, r, g, b);
}


//============================================================
//  renderer_d3d11::create
//============================================================

bool renderer_d3d11::create()
{
	if (!initialize())
	{
		osd_printf_error("Unable to initialize Direct3D11\n");
		return false;
	}

	return true;
}

void renderer_d3d11::toggle_fsfx()
{
	set_toggle(true);
}

void renderer_d3d11::record()
{
	if (m_shaders != nullptr)
	{
		m_shaders->record_movie();
	}
}

void renderer_d3d11::add_audio_to_recording(const int16_t *buffer, int samples_this_frame)
{
	if (m_shaders != nullptr)
	{
		m_shaders->record_audio(buffer, samples_this_frame);
	}
}

void renderer_d3d11::save()
{
	if (m_shaders != nullptr)
	{
		m_shaders->save_snapshot();
	}
}


//============================================================
//  renderer_d3d11::get_primitives
//============================================================

render_primitive_list *renderer_d3d11::get_primitives()
{
	RECT client;
	HWND hWnd = dynamic_cast<win_window_info &>(window()).platform_window();
	if (IsIconic(hWnd))
		return nullptr;

	GetClientRectExceptMenu(hWnd, &client, window().fullscreen());
	if (rect_width(&client) > 0 && rect_height(&client) > 0)
	{
		window().target()->set_bounds(rect_width(&client), rect_height(&client), window().pixel_aspect());

		const uint32_t curr_refresh_num = get_refresh_numerator();
		const uint32_t curr_refresh_denom = get_refresh_denominator();
		const float curr_refresh = curr_refresh_num / (float)curr_refresh_denom;

		const DXGI_RATIONAL dxgi_refresh = get_origmode().RefreshRate;
		const float mode_refresh = dxgi_refresh.Numerator / (float)dxgi_refresh.Denominator;
		window().target()->set_max_update_rate(curr_refresh_num == 0 ? mode_refresh : curr_refresh);
	}
	if (m_shaders != nullptr)
	{
		// do not transform primitives (scale, offset) if shaders are enabled, the shaders will handle the transformation
		window().target()->set_transform_container(!m_shaders->enabled());
	}
	return &window().target()->get_primitives();
}


//============================================================
//  renderer_d3d11::draw
//============================================================

bool renderer_d3d11::draw(const int update)
{
	if (!pre_window_draw_check())
		return false;

	begin_frame();

	// reset blend mode
	set_blendmode(BLENDMODE_NONE, true);

	// lock our vertex buffer
    memset(&m_lockedbuf, 0, sizeof(D3D11_MAPPED_SUBRESOURCE));
    m_d3d11_context->Map(m_vertexbuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m_lockedbuf);

    process_primitives();

	end_frame();

	return true;
}

void renderer_d3d11::set_texture(d3d11_texture_info *texture)
{
	if (texture != m_last_texture)
	{
		m_last_texture = texture;
		if (m_shaders->enabled())
		{
			m_shaders->set_texture(texture);
		}

		ID3D11ShaderResourceView * const *texture_view = (texture == nullptr) ? get_default_texture()->get_view() : texture->get_view();
		m_d3d11_context->PSSetShaderResources(0, 1, texture_view);
	}
}

void renderer_d3d11::set_sampler_mode(const uint32_t slot, const bool linear_filter, const D3D11_TEXTURE_ADDRESS_MODE mode, const bool force_set)
{
	if (mode != m_sampler_mode[slot] || linear_filter != m_linear_filter[slot] || force_set)
	{
		m_linear_filter[slot] = linear_filter;
		m_sampler_mode[slot] = mode;
		m_d3d11_context->PSSetSamplers(slot, 1, &m_sampler_states[(int)linear_filter][(int)mode - 1]);
	}
}

void renderer_d3d11::set_blendmode(const int blendmode, const bool force_set)
{
	if (m_blendmode != blendmode || m_blendmode == -1 || force_set)
	{
		m_blendmode = blendmode;
		m_d3d11_context->OMSetBlendState(m_blend_states[blendmode], nullptr, 0xffffffff);
	}
}

d3d11_texture_manager::d3d11_texture_manager(renderer_d3d11 &d3d)
	: m_renderer(d3d)
	, m_default_texture(nullptr)
{
	m_texture_max_width = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
	m_texture_max_height = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;

	// pick a YUV texture format
	m_yuv_format = DXGI_FORMAT_YUY2;

	// set the max texture size
	d3d.window().target()->set_max_texture_size(m_texture_max_width, m_texture_max_height);
	osd_printf_verbose("Direct3D11: Max texture size = %dx%d\n", m_texture_max_width, m_texture_max_height);
}

d3d11_texture_manager::~d3d11_texture_manager()
{
	for (auto &info : m_texture_list)
	{
		info.reset();
	}
	m_texture_list.clear();
}

void d3d11_texture_manager::create_resources()
{
	m_default_bitmap.allocate(8, 8);
	if (m_default_bitmap.valid())
	{
		m_default_bitmap.fill(rgb_t(0xff, 0xff, 0xff, 0xff));

		// fake in the basic data so it looks like it came from render.cpp
		render_texinfo texture;
		memset(&texture, 0, sizeof(texture));
		texture.base = m_default_bitmap.raw_pixptr(0);
		texture.rowpixels = m_default_bitmap.rowpixels();
		texture.width = m_default_bitmap.width();
		texture.height = m_default_bitmap.height();
		texture.palette = nullptr;
		texture.seqid = 0;
		texture.unique_id = ~0ULL;
		texture.old_id = ~0ULL;

		// now create it
		auto tex = std::make_unique<d3d11_texture_info>(*this, &texture, m_renderer.window().prescale(), PRIMFLAG_BLENDMODE(BLENDMODE_ALPHA) | PRIMFLAG_TEXFORMAT(TEXFORMAT_ARGB32));
		m_default_texture = tex.get();
		m_texture_list.push_back(std::move(tex));
	}
}

void d3d11_texture_manager::delete_resources()
{
	// is part of m_texlist and will be free'd there
	m_default_texture = nullptr;

	// free all textures
	m_texture_list.clear();
}

uint32_t d3d11_texture_manager::texture_compute_hash(const render_texinfo *texture, uint32_t flags)
{
	return (uintptr_t)texture->base ^ (flags & (PRIMFLAG_BLENDMODE_MASK | PRIMFLAG_TEXFORMAT_MASK));
}

d3d11_texture_info *d3d11_texture_manager::find_texinfo(const render_texinfo *texinfo, uint32_t flags)
{
	const uint32_t hash = texture_compute_hash(texinfo, flags);

	// find a match
	for (auto it = m_texture_list.begin(); it != m_texture_list.end(); it++)
	{
		const uint32_t test_screen = uint32_t((*it)->get_texinfo().unique_id >> 57);
		const uint32_t test_page = uint32_t((*it)->get_texinfo().unique_id >> 56) & 1;
		const uint32_t prim_screen = uint32_t(texinfo->unique_id >> 57);
		const uint32_t prim_page = uint32_t(texinfo->unique_id >> 56) & 1;
		if (test_screen != prim_screen || test_page != prim_page)
			continue;

		const bool hash_match = (*it)->get_hash() == hash;
		const bool base_match = (*it)->get_texinfo().base == texinfo->base;
		const bool width_match = (*it)->get_texinfo().width == texinfo->width;
		const bool height_match = (*it)->get_texinfo().height == texinfo->height;
		const bool flags_match = (((*it)->get_flags() ^ flags) & (PRIMFLAG_BLENDMODE_MASK | PRIMFLAG_TEXFORMAT_MASK)) == 0;
		if (hash_match && base_match && width_match && height_match && flags_match)
		{
			return (*it).get();
		}
	}

	return nullptr;
}

renderer_d3d11::renderer_d3d11(osd_window &window, const d3d11_create_fn create_fn, const d3d_compile_fn compile_fn)
	: osd_renderer(window)
	, m_create_fn(create_fn)
	, m_compile_fn(compile_fn)
	, m_d3d11(nullptr)
	, m_d3d11_context(nullptr)
	, m_factory(nullptr)
	, m_adapter(nullptr)
	, m_adapter_num(0)
	, m_output(nullptr)
	, m_output_num(0)
	, m_width(0)
	, m_height(0)
	, m_refresh_num(0)
	, m_refresh_denom(0)
	, m_post_fx_available(true)
	, m_sync_interval(0)
	, m_gamma_points(0)
	, m_gamma_min(0.f)
	, m_gamma_max(0.f)
	, m_pixformat()
	, m_vectorbatch(nullptr)
	, m_batchindex(0)
	, m_numpolys(0)
	, m_toggle(false)
	, m_last_texture(nullptr)
	, m_blendmode(-1)
	, m_force_render_states(true)
	, m_device_initialized(false)
	, m_framebuffer(nullptr)
	, m_framebuffer_view(nullptr)
	, m_depthbuffer(nullptr)
	, m_depthbuffer_view(nullptr)
	, m_vs(nullptr)
	, m_vs_bcg(nullptr)
	, m_vs_palette16(nullptr)
	, m_ps(nullptr)
	, m_ps_bcg(nullptr)
	, m_ps_palette16(nullptr)
	, m_rasterizer_state(nullptr)
	, m_depth_stencil_state(nullptr)
	, m_constant_buffer(nullptr)
	, m_input_layout(nullptr)
	, m_input_layout_bcg(nullptr)
	, m_input_layout_palette16(nullptr)
	, m_shaders(nullptr)
	, m_vertexbuf(nullptr)
	, m_numverts(0)
	, m_indexbuf(nullptr)
	, m_texture_manager()
{
	for (uint32_t i = 0; i < 16; i++)
	{
		m_linear_filter[i] = false;
		m_sampler_mode[i] = D3D11_TEXTURE_ADDRESS_BORDER;
	}
}

bool renderer_d3d11::initialize()
{
	osd_printf_verbose("Direct3D11: Initialize\n");

	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
	(m_create_fn)(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG,
		featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &m_d3d11, nullptr, &m_d3d11_context);

	// configure the adapter for the mode we want
	if (!config_adapter_mode())
		return false;

	// create the device immediately for the full screen case (defer for window mode in update_window_size())
	if (!device_create())
		return false;

	return true;
}

bool renderer_d3d11::pre_window_draw_check()
{
	// if we're in the middle of resizing, leave things alone
	if (dynamic_cast<win_window_info &>(window()).m_resize_state == win_window_info::RESIZE_STATE_RESIZING)
		return true;

	// check if shaders should be toggled
	if (m_device_initialized && m_toggle)
	{
		m_toggle = false;

		// free resources
		device_delete_resources();

		m_shaders->toggle();
		m_sliders_dirty = true;

		// re-create resources
		if (!device_create_resources())
		{
			osd_printf_verbose("Direct3D11: failed to recreate resources for device; failing permanently\n");
			return false;
		}
	}

	// if we have a device, check the cooperative level
	if (m_device_initialized && !device_test_cooperative())
		return false;

	// in window mode, we need to track the window size
	if (!window().fullscreen() || !m_device_initialized)
	{
		// if the size changes, skip this update since the render target will be out of date
		if (update_window_size())
			return true;

		// if we have no device, after updating the size, return an error so GDI can try
		if (!m_device_initialized)
			return false;
	}

	return true;
}

void d3d11_texture_manager::update_textures()
{
	for (render_primitive &prim : *m_renderer.window().m_primlist)
	{
		if (prim.texture.base != nullptr)
		{
			d3d11_texture_info *texture = find_texinfo(&prim.texture, prim.flags);
			if (texture == nullptr)
			{
				const int prescale = m_renderer.get_shaders()->enabled() ? 1 : m_renderer.window().prescale();
				auto tex = std::make_unique<d3d11_texture_info>(*this, &prim.texture, prescale, prim.flags);
				texture = tex.get();
				m_texture_list.push_back(std::move(tex));
			}
			else
			{
				// if there is one, but with a different seqid, copy the data
				if (texture->get_texinfo().seqid != prim.texture.seqid)
				{
					texture->set_data(&prim.texture, prim.flags);
					texture->get_texinfo().seqid = prim.texture.seqid;
				}
			}
		}
	}

	if (!m_renderer.get_shaders()->enabled())
		return;

	int screen_index = 0;
	for (render_primitive &prim : *m_renderer.window().m_primlist)
	{
		if (PRIMFLAG_GET_SCREENTEX(prim.flags))
		{
			if (!m_renderer.get_shaders()->get_texture_target(&prim, prim.texture.width, prim.texture.height, screen_index))
			{
				if (!m_renderer.get_shaders()->create_texture_target(&prim, prim.texture.width, prim.texture.height, screen_index))
				{
					m_renderer.set_post_fx_unavailable();
					break;
				}
			}
			screen_index++;
		}
		else if (PRIMFLAG_GET_VECTORBUF(prim.flags))
		{
			if (!m_renderer.get_shaders()->get_vector_target(&prim, screen_index))
			{
				if (!m_renderer.get_shaders()->create_vector_target(&prim, screen_index))
				{
					m_renderer.set_post_fx_unavailable();
					break;
				}
			}
			screen_index++;
		}
	}
}

void renderer_d3d11::begin_frame()
{
	m_force_render_states = true;

	float background_color[4] = { 0.f, 0.f, 0.f, 1.f };
	m_d3d11_context->ClearRenderTargetView(m_framebuffer_view, background_color);
	m_d3d11_context->ClearDepthStencilView(m_depthbuffer_view, D3D11_CLEAR_DEPTH, 1.f, 0);

	window().m_primlist->acquire_lock();

	// first update any textures
	m_texture_manager->update_textures();

	// ensure we have a full-screen quad
	init_blit_quad();

	if (m_shaders->enabled())
		m_shaders->begin_frame(window().m_primlist);

	m_force_render_states = true;
}

void renderer_d3d11::process_primitives()
{
	// loop over line primitives
	int vector_count = 0;
	for (render_primitive &prim : *window().m_primlist)
	{
		if (prim.type == render_primitive::LINE && PRIMFLAG_GET_VECTOR(prim.flags))
		{
			vector_count++;
		}
	}

	// Rotating index for vector time offsets
	for (render_primitive &prim : *window().m_primlist)
	{
		switch (prim.type)
		{
			case render_primitive::LINE:
				if (PRIMFLAG_GET_VECTOR(prim.flags))
				{
					if (vector_count > 0)
					{
						batch_vectors(vector_count);
						vector_count = 0;
					}
				}
				else
				{
					draw_line(prim);
				}
				break;

			case render_primitive::QUAD:
				draw_quad(prim);
				break;

			default:
				throw emu_fatalerror("Unexpected render_primitive type");
		}
	}
}

void renderer_d3d11::end_frame()
{
	window().m_primlist->release_lock();

	// flush any pending polygons
	primitive_flush_pending();

	if (m_shaders->enabled())
		m_shaders->end_frame();

	m_d3d11_context->Unmap(m_vertexbuf, 0);

	// present the current buffers
	m_d3d11_context->Flush();
	HRESULT result = m_swap_chain->Present(/*m_sync_interval*/0, 0);
	if (FAILED(result))
		osd_printf_error("Direct3D11: Present call failed: %08x\n", (uint32_t)result);
}

void renderer_d3d11::update_presentation_parameters()
{
	// identify the actual window; this is needed so that -attach_window
	// can work on a non-root HWND
	HWND device_hwnd = GetAncestor(dynamic_cast<win_window_info &>(window()).platform_window(), GA_ROOT);
	memset(&m_presentation, 0, sizeof(m_presentation));
	m_presentation.BufferDesc.Width = m_width;
	m_presentation.BufferDesc.Height = m_height;
	m_presentation.BufferDesc.RefreshRate.Numerator = m_refresh_num;
	m_presentation.BufferDesc.RefreshRate.Denominator = m_refresh_denom;
	m_presentation.BufferDesc.Format = m_pixformat;
	m_presentation.SampleDesc.Count = 1;
	m_presentation.SampleDesc.Quality = 0;
	m_presentation.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	m_presentation.BufferCount = 2;
	m_presentation.OutputWindow = device_hwnd;
	m_presentation.Windowed = !window().fullscreen();
	m_presentation.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	m_presentation.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	if ((video_config.triplebuf && window().fullscreen()) || video_config.waitvsync || video_config.syncrefresh)
		m_sync_interval = 1;
	else
		m_sync_interval = 0;
}


void renderer_d3d11::update_gamma_ramp()
{
	if (m_gamma_points < 2)
	{
		printf("Bailing early from update_gamma_ramp\n");
		return;
	}

	// set the gamma if we need to
	if (window().fullscreen())
	{
		// only set the gamma if it's not 1.0
		auto &options = downcast<windows_options &>(window().machine().options());
		float brightness = options.full_screen_brightness();
		float contrast = options.full_screen_contrast();
		float gamma = options.full_screen_gamma();
		if (brightness != 1.0f || contrast != 1.0f || gamma != 1.0f)
		{
			DXGI_GAMMA_CONTROL gamma_control;
			gamma_control.Scale = { 1.f, 1.f, 1.f };
			gamma_control.Offset = { 0.f, 0.f, 0.f };

			const float gamma_range = m_gamma_max - m_gamma_min;
			for (uint32_t i = 0; i < m_gamma_points; i++)
			{
				const float percent = (float)i / (m_gamma_points - 1);
				const uint8_t gamma_index = (uint8_t)std::round(percent * 255);
				const float gamma_out = apply_brightness_contrast_gamma(gamma_index, brightness, contrast, gamma) * gamma_range;
				gamma_control.GammaCurve[i] = { gamma_out, gamma_out, gamma_out };
			}

			m_output->SetGammaControl(&gamma_control);
		}
	}
}


//============================================================
//  device_create
//============================================================

bool renderer_d3d11::device_create()
{
	// if a device exists, free it
	if (m_device_initialized)
		device_delete_resources();

	// initialize the D3D11 presentation parameters
	update_presentation_parameters();

	// verify gamma caps
	device_verify_gamma_caps();

	// initialize our swap chain
	HRESULT result = m_factory->CreateSwapChain(m_d3d11, &m_presentation, &m_swap_chain);
	if (FAILED(result))
	{
		osd_printf_error("Direct3D11: CreateSwapChain call failed: %08x\n", (uint32_t)result);
		return false;
	}

	m_texture_manager = std::make_unique<d3d11_texture_manager>(*this);

	m_device_initialized = true;

	if (!device_create_resources())
		return false;

	return true;
}


//============================================================
//  device_create_resources
//============================================================

bool renderer_d3d11::device_create_resources()
{
	// initialize our framebuffer and view
	HRESULT result = m_swap_chain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void **>(&m_framebuffer));
	if (FAILED(result))
	{
		osd_printf_error("m_swap_chain->GetBuffer failed, %08x\n", (uint32_t)result);
		return false;
	}
	m_d3d11->CreateRenderTargetView(m_framebuffer, nullptr, &m_framebuffer_view);

	// initialize our depthbuffer and view
	D3D11_TEXTURE2D_DESC depth_buffer_desc;
	memset(&depth_buffer_desc, 0, sizeof(depth_buffer_desc));
	m_framebuffer->GetDesc(&depth_buffer_desc); // copy from framebuffer properties
	depth_buffer_desc.Format    = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depth_buffer_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	m_d3d11->CreateTexture2D(&depth_buffer_desc, nullptr, &m_depthbuffer);
	m_d3d11->CreateDepthStencilView(m_depthbuffer, nullptr, &m_depthbuffer_view);

	// Pre-initialize our input layout descriptor
	D3D11_INPUT_ELEMENT_DESC input_desc[4];
	input_desc[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,                            0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
	input_desc[1] = { "COLOR",    0, DXGI_FORMAT_B8G8R8A8_UNORM,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 };
	input_desc[2] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 };
	input_desc[3] = { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 };

	// initialize our default shaders
	if (!load_shader("default_d3d11.hlsl", &m_vs, &m_ps, input_desc, &m_input_layout))
		return false;
	if (!load_shader("apply_bcg.hlsl", &m_vs_bcg, &m_ps_bcg, input_desc, &m_input_layout_bcg))
		return false;
	if (!load_shader("apply_palette16.hlsl", &m_vs_palette16, &m_ps_palette16, input_desc, &m_input_layout_palette16))
		return false;

	// initialize our rasterizer state
	D3D11_RASTERIZER_DESC rasterizer_desc = {};
	rasterizer_desc.FillMode = D3D11_FILL_SOLID;
	rasterizer_desc.CullMode = D3D11_CULL_NONE;
	m_d3d11->CreateRasterizerState(&rasterizer_desc, &m_rasterizer_state);

	// initialize our depth-stencil state
	D3D11_DEPTH_STENCIL_DESC depth_stencil_desc = {};
	depth_stencil_desc.DepthEnable    = false;
	depth_stencil_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depth_stencil_desc.DepthFunc      = D3D11_COMPARISON_ALWAYS;
	m_d3d11->CreateDepthStencilState(&depth_stencil_desc, &m_depth_stencil_state);

	// initialize our constant buffer
	D3D11_BUFFER_DESC constant_buffer_desc = { 0 };
	constant_buffer_desc.ByteWidth      = (sizeof(pipeline_constants) + 0xf) & 0xfffffff0; // round constant buffer size to a 16-byte boundary
	constant_buffer_desc.Usage          = D3D11_USAGE_DYNAMIC;
	constant_buffer_desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
	constant_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	m_d3d11->CreateBuffer(&constant_buffer_desc, nullptr, &m_constant_buffer);

	// initialize our vertex buffer
	D3D11_BUFFER_DESC vertex_buffer_desc = { 0 };
	vertex_buffer_desc.ByteWidth      = sizeof(d3d11_vertex) * VERTEX_BUFFER_LENGTH;
	vertex_buffer_desc.Usage          = D3D11_USAGE_DYNAMIC;
	vertex_buffer_desc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
    vertex_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	D3D11_SUBRESOURCE_DATA vertex_data = { 0 };
	vertex_data.pSysMem = m_vertex_data;

	result = m_d3d11->CreateBuffer(&vertex_buffer_desc, &vertex_data, &m_vertexbuf);
	if (FAILED(result))
	{
		osd_printf_error("Direct3D11: CreateBuffer failed: %08x\n", (uint32_t)result);
		return false;
	}

	// initialize our index buffer
	D3D11_BUFFER_DESC index_buffer_desc = { 0 };
	index_buffer_desc.ByteWidth = sizeof(uint32_t) * (VERTEX_BUFFER_LENGTH / 4) * 6; // 3 vertices per triangle, 2 triangles per quad
	index_buffer_desc.Usage     = D3D11_USAGE_IMMUTABLE;
	index_buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

	uint32_t base_index = 0;
	for (uint32_t i = 0; i < (VERTEX_BUFFER_LENGTH / 4) * 6; i += 6, base_index += 4)
	{
		m_index_data[i + 0] = base_index;
		m_index_data[i + 1] = base_index + 1;
		m_index_data[i + 2] = base_index + 3;
		m_index_data[i + 3] = base_index + 3;
		m_index_data[i + 4] = base_index + 2;
		m_index_data[i + 5] = base_index;
	}

	D3D11_SUBRESOURCE_DATA index_data = { 0 };
	index_data.pSysMem = m_index_data;
	m_d3d11->CreateBuffer(&index_buffer_desc, &index_data, &m_indexbuf);

	// assign our viewport
	m_viewport = { 0.f, 0.f, float(depth_buffer_desc.Width), float(depth_buffer_desc.Height), 0.f, 1.f };

	device_create_blend_states();
	device_create_sampler_states();

	update_gamma_ramp();

	// create shaders only once
	if (!m_shaders)
		m_shaders = std::make_unique<d3d11_shaders>();

	if (m_shaders->init(m_d3d11, m_d3d11_context, m_compile_fn, &window().machine(), this))
	{
		m_post_fx_available = true;
		m_shaders->init_slider_list();
		m_sliders_dirty = true;
	}

	// create resources
	if (!m_shaders->create_resources())
	{
		osd_printf_verbose("Direct3D: failed to create HLSL resources for device\n");
		return false;
	}

	m_force_render_states = true;

	m_texture_manager->create_resources();

	return true;
}


//============================================================
//  device_create_blend_states
//============================================================

bool renderer_d3d11::load_shader(const char *filename, ID3D11VertexShader **vs, ID3D11PixelShader **ps,
	D3D11_INPUT_ELEMENT_DESC *layout_desc, ID3D11InputLayout **layout)
{
	const char *hlsl_dir = downcast<windows_options &>(window().machine().options()).screen_post_fx_dir();

	char name_cstr[1024];
	sprintf(name_cstr, "%s\\%s", hlsl_dir, filename);
	auto effect_name = osd::text::to_tstring(name_cstr);

	ID3DBlob *vs_results = nullptr;
	ID3DBlob *vs_blob = nullptr;
	HRESULT result = (m_compile_fn)(effect_name.c_str(), nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vs_blob, &vs_results);
	if (FAILED(result))
	{
		osd_printf_error("Direct3D11: VS compilation failed for %ls: %08x\n", effect_name.c_str(), (uint32_t)result);
		const char *results = (const char *)vs_results->GetBufferPointer();
		size_t results_size = (size_t)vs_results->GetBufferSize();
		for (size_t i = 0; i < results_size; i++)
		{
			osd_printf_error("%c", results[i]);
		}
		osd_printf_error("\n");
		return false;
	}
	m_d3d11->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, vs);

	ID3DBlob *ps_results = nullptr;
	ID3DBlob *ps_blob = nullptr;
	result = (m_compile_fn)(effect_name.c_str(), nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &ps_blob, &ps_results);
	if (FAILED(result))
	{
		osd_printf_error("Direct3D11: PS compilation failed for %ls: %08x\n", effect_name.c_str(), (uint32_t)result);
		const char *results = (const char *)ps_results->GetBufferPointer();
		size_t results_size = (size_t)ps_results->GetBufferSize();
		for (size_t i = 0; i < results_size; i++)
		{
			osd_printf_error("%c", results[i]);
		}
		osd_printf_error("\n");
		vs_blob->Release();
		return false;
	}
	m_d3d11->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, ps);

	m_d3d11->CreateInputLayout(layout_desc, 4, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), layout);

	vs_blob->Release();
	ps_blob->Release();

	return true;
}


//============================================================
//  init_blit_quad
//
//  Called at the start of each frame so that the two
//  triangles used for the full-screen blits are always
//  at the beginning of the vertex buffer.
//============================================================

void renderer_d3d11::init_blit_quad()
{
	d3d11_vertex *vertbuf = mesh_alloc(4);
	if (vertbuf == nullptr)
		return;

	// fill in the vertexes clockwise
	vertbuf[0].x = 0.f;
	vertbuf[0].y = 0.f;
	vertbuf[1].x = m_width;
	vertbuf[1].y = 0.f;
	vertbuf[2].x = 0.f;
	vertbuf[2].y = m_height;
	vertbuf[3].x = m_width;
	vertbuf[3].y = m_height;

	vertbuf[0].u0 = 0.f;
	vertbuf[0].v0 = 0.f;

	vertbuf[1].u0 = 1.f;
	vertbuf[1].v0 = 0.f;

	vertbuf[2].u0 = 0.f;
	vertbuf[2].v0 = 1.f;

	vertbuf[3].u0 = 1.f;
	vertbuf[3].v0 = 1.f;

	vertbuf[0].u1 = 0.f;
	vertbuf[0].v1 = 0.f;
	vertbuf[1].u1 = 0.f;
	vertbuf[1].v1 = 0.f;
	vertbuf[2].u1 = 0.f;
	vertbuf[2].v1 = 0.f;
	vertbuf[3].u1 = 0.f;
	vertbuf[3].v1 = 0.f;

	// set the color and Z parameters
	for (int i = 0; i < 4; i++)
	{
		vertbuf[i].z = 0.f;
		vertbuf[i].r = 255;
		vertbuf[i].g = 255;
		vertbuf[i].b = 255;
		vertbuf[i].a = 255;
	}
}


//============================================================
//  device_create_blend_states
//============================================================

void renderer_d3d11::device_create_blend_states()
{
	D3D11_RENDER_TARGET_BLEND_DESC none_rt_blend_desc =
	{
		false, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL
	};
	D3D11_RENDER_TARGET_BLEND_DESC alpha_rt_blend_desc =
	{
		true, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL
	};
	D3D11_RENDER_TARGET_BLEND_DESC multiply_rt_blend_desc =
	{
		true, D3D11_BLEND_DEST_COLOR, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL
	};
	D3D11_RENDER_TARGET_BLEND_DESC add_rt_blend_desc =
	{
		true, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL
	};
	D3D11_BLEND_DESC none_desc = { false, false, { none_rt_blend_desc } };
	D3D11_BLEND_DESC alpha_desc = { false, false, { alpha_rt_blend_desc } };
	D3D11_BLEND_DESC multiply_desc = { false, true, { multiply_rt_blend_desc } };
	D3D11_BLEND_DESC add_desc = { false, false, { add_rt_blend_desc } };
	m_d3d11->CreateBlendState(&none_desc, &m_blend_states[BLENDMODE_NONE]);
	m_d3d11->CreateBlendState(&alpha_desc, &m_blend_states[BLENDMODE_ALPHA]);
	m_d3d11->CreateBlendState(&multiply_desc, &m_blend_states[BLENDMODE_RGB_MULTIPLY]);
	m_d3d11->CreateBlendState(&add_desc, &m_blend_states[BLENDMODE_ADD]);
}


//============================================================
//  device_create_sampler_states
//============================================================

void renderer_d3d11::device_create_sampler_states()
{
	D3D11_SAMPLER_DESC desc;
	memset(&desc, 0, sizeof(desc));
	desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_POINT;
	desc.AddressU       = D3D11_TEXTURE_ADDRESS_WRAP;
	desc.AddressV       = D3D11_TEXTURE_ADDRESS_WRAP;
	desc.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
	desc.MaxAnisotropy  = 1;
	desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	desc.MinLOD         = -FLT_MAX;
	desc.MaxLOD         = FLT_MAX;
	m_d3d11->CreateSamplerState(&desc, &m_sampler_states[0][(int)D3D11_TEXTURE_ADDRESS_WRAP - 1]);

	desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	m_d3d11->CreateSamplerState(&desc, &m_sampler_states[1][(int)D3D11_TEXTURE_ADDRESS_WRAP - 1]);

	desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_POINT;
	desc.AddressU       = D3D11_TEXTURE_ADDRESS_MIRROR;
	desc.AddressV       = D3D11_TEXTURE_ADDRESS_MIRROR;
	desc.AddressW       = D3D11_TEXTURE_ADDRESS_MIRROR;
	m_d3d11->CreateSamplerState(&desc, &m_sampler_states[0][(int)D3D11_TEXTURE_ADDRESS_MIRROR - 1]);

	desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	m_d3d11->CreateSamplerState(&desc, &m_sampler_states[1][(int)D3D11_TEXTURE_ADDRESS_MIRROR- 1]);

	desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_POINT;
	desc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
	desc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
	desc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
	m_d3d11->CreateSamplerState(&desc, &m_sampler_states[0][(int)D3D11_TEXTURE_ADDRESS_CLAMP - 1]);

	desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	m_d3d11->CreateSamplerState(&desc, &m_sampler_states[1][(int)D3D11_TEXTURE_ADDRESS_CLAMP - 1]);
}


//============================================================
//  destructor
//============================================================

renderer_d3d11::~renderer_d3d11()
{
	// free our base resources
	if (m_device_initialized)
		device_delete_resources();
	//m_texture_manager.reset();

	// free the device itself
	//m_d3d11_context.Reset();
	//m_d3d11.Reset();
}


//============================================================
//  device_delete_resources
//============================================================

void renderer_d3d11::device_delete_resources()
{
	m_d3d11_context->ClearState();

	m_texture_manager.reset();

	m_sampler_states[1][2]->Release();
	m_sampler_states[1][1]->Release();
	m_sampler_states[1][0]->Release();
	m_sampler_states[0][2]->Release();
	m_sampler_states[0][1]->Release();
	m_sampler_states[0][0]->Release();

	m_blend_states[BLENDMODE_ADD]->Release();
	m_blend_states[BLENDMODE_RGB_MULTIPLY]->Release();
	m_blend_states[BLENDMODE_ALPHA]->Release();
	m_blend_states[BLENDMODE_NONE]->Release();

	m_indexbuf->Release();
	m_vertexbuf->Release();
	m_input_layout->Release();
	m_constant_buffer->Release();

	m_depth_stencil_state->Release();
	m_rasterizer_state->Release();

	m_ps_palette16->Release();
	m_ps_bcg->Release();
	m_ps->Release();

	m_vs_palette16->Release();
	m_vs_bcg->Release();
	m_vs->Release();

	m_depthbuffer_view->Release();
	m_depthbuffer->Release();
	m_framebuffer_view->Release();
	m_framebuffer->Release();

	m_swap_chain->Release();
	m_output->Release();
	m_adapter->Release();
	m_factory->Release();

	m_d3d11_context->Flush();

	m_d3d11_context->Release();
	m_d3d11_context = nullptr;

	m_device_initialized = false;
}


//============================================================
//  device_verify_gamma_caps
//============================================================

bool renderer_d3d11::device_verify_gamma_caps()
{
	//DXGI_GAMMA_CONTROL_CAPABILITIES gamma_caps;
	//HRESULT result = m_output->GetGammaControlCapabilities(&gamma_caps);
	//if (FAILED(result))
	//{
		//osd_printf_verbose("Direct3D11: Error %08lX during GetGammaControlCapabilities call\n", result);
		//return false;
	//}

	m_gamma_points = 0;//gamma_caps.NumGammaControlPoints;
	if (m_gamma_points < 2)
		osd_printf_warning("Direct3D11: Warning - device does not support full screen gamma correction.\n");

	return true;
}


//============================================================
//  device_test_cooperative
//============================================================

bool renderer_d3d11::device_test_cooperative()
{
	// check our current status; if we lost the device, punt to GDI
	HRESULT result = m_swap_chain->Present(m_sync_interval, DXGI_PRESENT_TEST);
	if (result == DXGI_ERROR_DEVICE_REMOVED)
		return false;

	// if we need to reset ourselves, try it
	if (result == DXGI_ERROR_DEVICE_RESET)
	{
		osd_printf_verbose("Direct3D: resetting device\n");

		// free all existing resources
		if (m_device_initialized)
			device_delete_resources();

		// try to create the resources again; if that didn't work, delete the whole thing
		if (!device_create_resources())
		{
			osd_printf_verbose("Direct3D: failed to recreate resources for device; failing permanently\n");
			device_delete_resources();
			return false;
		}
	}

	return true;
}


//============================================================
//  config_adapter_mode
//============================================================

bool renderer_d3d11::config_adapter_mode()
{
	// choose the monitor number
	DXGI_ADAPTER_DESC adapter_desc;
	if (!get_adapter_for_monitor(&adapter_desc))
		return false;

	// get the identifier
	std::wstring wide_description(adapter_desc.Description);
	std::string description(wide_description.begin(), wide_description.end());
	osd_printf_verbose("Direct3D11: Configuring adapter #%d = %s\n", m_adapter_num, description.c_str());

	// find the highest-res mode supported by the user's adapter that can hit 60Hz
	// reasoning: avoid 4K displays attached over a 30Hz HDMI uplink
	uint32_t largest_mode_for_60 = 0;
	uint32_t mode_count = 0;
	if (FAILED(m_output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &mode_count, nullptr)))
		return false;

	std::unique_ptr<DXGI_MODE_DESC []> modes = std::make_unique<DXGI_MODE_DESC []>(mode_count);
	if (!FAILED(m_output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &mode_count, &modes[0])))
	{
		uint32_t largest_area = 0;
		for (uint32_t i = 0; i < mode_count; i++)
		{
			DXGI_MODE_DESC &mode = modes[i];
			const float refresh_rate = mode.RefreshRate.Numerator / (float)mode.RefreshRate.Denominator;
			if (refresh_rate >= (1.01f / 60.f))
				continue;

			const uint32_t area = mode.Width * mode.Height;
			if (area < largest_area)
				continue;

			largest_mode_for_60 = i;
		}
	}
	m_origmode = modes[largest_mode_for_60];

	if (!window().fullscreen() || !video_config.switchres)
	{
		// choose a resolution: window mode case

		// bounds are from the window client rect
		RECT client;
		GetClientRectExceptMenu(dynamic_cast<win_window_info &>(window()).platform_window(), &client, window().fullscreen());
		m_width = client.right - client.left;
		m_height = client.bottom - client.top;

		// pix format is from the current mode
		m_pixformat = m_origmode.Format;
		m_refresh_num = 0;
		m_refresh_denom = 1;
	}
	else
	{
		// choose a resolution: full screen mode case

		// default to the current mode exactly
		m_width = m_origmode.Width;
		m_height = m_origmode.Height;
		m_pixformat = m_origmode.Format;
		m_refresh_num = m_origmode.RefreshRate.Numerator;
		m_refresh_denom = m_origmode.RefreshRate.Denominator;

		// if we're allowed to switch resolutions, override with something better
		if (video_config.switchres)
		{
			pick_best_mode(mode_count, modes);
		}
	}

	modes.reset();
	return true;
}


//============================================================
//  get_adapter_for_monitor
//============================================================

bool renderer_d3d11::get_adapter_for_monitor(DXGI_ADAPTER_DESC *adapter_desc)
{
	Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
	m_d3d11->QueryInterface(IID_IDXGIDevice, (void **)&dxgi_device);

	m_adapter_num = 0;
	m_output_num = 0;

	if (dxgi_device)
	{
		m_adapter = nullptr;
		dxgi_device->GetAdapter(&m_adapter);

		if (m_adapter)
		{
			IDXGIFactory *idxgi_factory = nullptr;
			m_adapter->GetParent(IID_IDXGIFactory, (void **)&idxgi_factory);

			if (idxgi_factory)
			{
				uint32_t adapter_index = 0;
				IDXGIAdapter *adapter;
				std::vector<IDXGIAdapter *> adapters;
				while (idxgi_factory->EnumAdapters(adapter_index, &adapter) != DXGI_ERROR_NOT_FOUND)
				{
					adapters.push_back(adapter);
					adapter_index++;
				}


				for (int adapternum = 0; adapternum < adapters.size(); adapternum++)
				{
					IDXGIAdapter *adapter = adapters[adapternum];
					uint32_t output_index = 0;
					IDXGIOutput *output;
					while (adapter->EnumOutputs(output_index, &output) != DXGI_ERROR_NOT_FOUND)
					{
						if (output->GetDesc(&m_output_desc) == S_OK && m_output_desc.Monitor == reinterpret_cast<HMONITOR>(window().monitor()->oshandle()))
						{
							if (FAILED(adapter->GetDesc(adapter_desc)))
								return false;

							m_factory = idxgi_factory;
							m_adapter = adapter;
							m_adapter_num = adapternum;
							m_output = output;
							m_output_num = (int)output_index;
							dxgi_device->Release();
							return true;
						}
						output_index++;
					}
				}
			}
		}
	}

	dxgi_device->Release();
	return false;
}


//============================================================
//  pick_best_mode
//============================================================

void renderer_d3d11::pick_best_mode(uint32_t mode_count, std::unique_ptr<DXGI_MODE_DESC []> &modes)
{
	double target_refresh = 60.0;
	int32_t minwidth, minheight;
	float best_score = 0.0f;

	// determine the refresh rate of the primary screen
	const screen_device *primary_screen = screen_device_enumerator(window().machine().root_device()).first();
	if (primary_screen != nullptr)
		target_refresh = ATTOSECONDS_TO_HZ(primary_screen->refresh_attoseconds());

	// determine the minimum width/height for the selected target
	// note: technically we should not be calling this from an alternate window
	// thread; however, it is only done during init time, and the init code on
	// the main thread is waiting for us to finish, so it is safe to do so here
	window().target()->compute_minimum_size(minwidth, minheight);

	// use those as the target for now
	int32_t target_width = minwidth;
	int32_t target_height = minheight;

	// enumerate all the video modes and find the best match
	osd_printf_verbose("Direct3D11: Selecting video mode...\n");
	for (uint32_t mode_num = 0; mode_num < mode_count; mode_num++)
	{
		DXGI_MODE_DESC &mode = modes[mode_num];

		// compute initial score based on difference between target and current
		float size_score = 1.0f / (1.0f + fabs((float)(mode.Width - target_width)) + fabs((float)(mode.Height - target_height)));

		// if the mode is too small, give a big penalty
		if (mode.Width < minwidth || mode.Height < minheight)
			size_score *= 0.01f;

		// if mode is smaller than we'd like, it only scores up to 0.1
		if (mode.Width < target_width || mode.Height < target_height)
			size_score *= 0.1f;

		// if we're looking for a particular mode, that's a winner
		if (mode.Width == window().m_win_config.width && mode.Height == window().m_win_config.height)
			size_score = 2.0f;

		// compute refresh score
		const float refresh_rate = mode.RefreshRate.Numerator / (float)mode.RefreshRate.Denominator;
		float refresh_score = 1.0f / (1.0f + fabs(refresh_rate - target_refresh));

		// if refresh is smaller than we'd like, it only scores up to 0.1
		if (refresh_rate < target_refresh)
			refresh_score *= 0.1f;

		// if we're looking for a particular refresh, make sure it matches
		if ((int)refresh_rate == window().m_win_config.refresh)
			refresh_score = 2.0f;

		// weight size and refresh equally
		float final_score = size_score + refresh_score;

		// best so far?
		osd_printf_verbose("  %4dx%4d @ %3d/%3dHz -> %f\n", mode.Width, mode.Height, mode.RefreshRate.Numerator, mode.RefreshRate.Denominator, final_score * 1000.0f);
		if (final_score > best_score)
		{
			best_score = final_score;
			m_width = mode.Width;
			m_height = mode.Height;
			m_pixformat = mode.Format;
			m_refresh_num = mode.RefreshRate.Numerator;
			m_refresh_denom = mode.RefreshRate.Denominator;
		}
	}
	osd_printf_verbose("Direct3D: Mode selected = %4dx%4d @ %3d/%3dHz\n", m_width, m_height, m_refresh_num, m_refresh_denom);
}


//============================================================
//  update_window_size
//============================================================

bool renderer_d3d11::update_window_size()
{
	// get the current window bounds
	win_window_info &win = dynamic_cast<win_window_info &>(window());
	RECT client;
	GetClientRectExceptMenu(win.platform_window(), &client, window().fullscreen());

	// if we have a device and matching width/height, nothing to do
	if (m_device_initialized && rect_width(&client) == m_width && rect_height(&client) == m_height)
	{
		// clear out any pending resizing if the area didn't change
		if (win.m_resize_state == win_window_info::RESIZE_STATE_PENDING)
			win.m_resize_state = win_window_info::RESIZE_STATE_NORMAL;
		return false;
	}

	// if we're in the middle of resizing, leave it alone as well
	if (win.m_resize_state == win_window_info::RESIZE_STATE_RESIZING)
		return false;

	m_width = rect_width(&client);
	m_height = rect_height(&client);
	if (device_create())
		return false;

	// reset the resize state to normal, and indicate we made a change
	win.m_resize_state = win_window_info::RESIZE_STATE_NORMAL;
	return true;
}


//============================================================
//  batch_vectors
//============================================================

void renderer_d3d11::batch_vectors(int vector_count)
{
	float quad_width = 0.0f;
	float quad_height = 0.0f;
	float target_width = 0.0f;
	float target_height = 0.0f;

	int index_count = vector_count * 6;
	int vertex_count = vector_count * 4;
	int triangle_count = vector_count * 2;
	m_vectorbatch = mesh_alloc(vertex_count);
	m_batchindex = 0;
	uint32_t tint = 0xffffffff;

	uint32_t cached_flags = 0;
	for (render_primitive &prim : *window().m_primlist)
	{
		switch (prim.type)
		{
			case render_primitive::LINE:
				if (PRIMFLAG_GET_VECTOR(prim.flags))
				{
					batch_vector(prim);
					cached_flags = prim.flags;

					const uint8_t a = (uint8_t)std::round(prim.color.a * 255);
					const uint8_t r = (uint8_t)std::round(prim.color.r * 255);
					const uint8_t g = (uint8_t)std::round(prim.color.g * 255);
					const uint8_t b = (uint8_t)std::round(prim.color.b * 255);
					tint = (a << 24) | (b << 16) | (g << 8) | r;
				}
				break;

			case render_primitive::QUAD:
				if (PRIMFLAG_GET_VECTORBUF(prim.flags))
				{
					quad_width = prim.get_quad_width();
					quad_height = prim.get_quad_height();
					target_width = prim.get_full_quad_width();
					target_height = prim.get_full_quad_height();

					const uint8_t a = (uint8_t)std::round(prim.color.a * 255);
					const uint8_t r = (uint8_t)std::round(prim.color.r * 255);
					const uint8_t g = (uint8_t)std::round(prim.color.g * 255);
					const uint8_t b = (uint8_t)std::round(prim.color.b * 255);
					tint = (a << 24) | (b << 16) | (g << 8) | r;
				}
				break;

			default:
				// Skip
				break;
		}
	}

	// handle orientation and rotation for vectors as they were a texture
	if (m_shaders->enabled())
	{
		bool orientation_swap_xy =
			(window().machine().system().flags & ORIENTATION_SWAP_XY) == ORIENTATION_SWAP_XY;
		bool rotation_swap_xy =
			(window().target()->orientation() & ORIENTATION_SWAP_XY) == ORIENTATION_SWAP_XY;
		bool swap_xy = orientation_swap_xy ^ rotation_swap_xy;

		bool rotation_0 = window().target()->orientation() == ROT0;
		bool rotation_90 = window().target()->orientation() == ROT90;
		bool rotation_180 = window().target()->orientation() == ROT180;
		bool rotation_270 = window().target()->orientation() == ROT270;
		bool flip_x =
			((rotation_0 || rotation_270) && orientation_swap_xy) ||
			((rotation_180 || rotation_270) && !orientation_swap_xy);
		bool flip_y =
			((rotation_0 || rotation_90) && orientation_swap_xy) ||
			((rotation_180 || rotation_90) && !orientation_swap_xy);

		float screen_width = float(this->get_width());
		float screen_height = float(this->get_height());
		float half_screen_width = screen_width * 0.5f;
		float half_screen_height = screen_height * 0.5f;
		float screen_swap_x_factor = 1.0f / screen_width * screen_height;
		float screen_swap_y_factor = 1.0f / screen_height * screen_width;
		float screen_target_ratio_x = screen_width / target_width;
		float screen_target_ratio_y = screen_height / target_height;

		if (swap_xy)
		{
			std::swap(screen_target_ratio_x, screen_target_ratio_y);
		}

		for (int batchindex = 0; batchindex < m_batchindex; batchindex++)
		{
			if (swap_xy)
			{
				m_vectorbatch[batchindex].x *= screen_swap_x_factor;
				m_vectorbatch[batchindex].y *= screen_swap_y_factor;
				std::swap(m_vectorbatch[batchindex].x, m_vectorbatch[batchindex].y);
			}

			if (flip_x)
			{
				m_vectorbatch[batchindex].x = screen_width - m_vectorbatch[batchindex].x;
			}

			if (flip_y)
			{
				m_vectorbatch[batchindex].y = screen_height - m_vectorbatch[batchindex].y;
			}

			// center
			m_vectorbatch[batchindex].x -= half_screen_width;
			m_vectorbatch[batchindex].y -= half_screen_height;

			// correct screen/target ratio (vectors are created in screen coordinates and have to be adjusted for texture coordinates of the target)
			m_vectorbatch[batchindex].x *= screen_target_ratio_x;
			m_vectorbatch[batchindex].y *= screen_target_ratio_y;

			// un-center
			m_vectorbatch[batchindex].x += half_screen_width;
			m_vectorbatch[batchindex].y += half_screen_height;
		}
	}

	// now add a polygon entry
	m_poly[m_numpolys].init(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, triangle_count, index_count, vertex_count, cached_flags, nullptr, quad_width, quad_height, tint);
	m_numpolys++;
}

void renderer_d3d11::batch_vector(const render_primitive &prim)
{
	// get a pointer to the vertex buffer
	if (m_vectorbatch == nullptr)
		return;

	// compute the effective width based on the direction of the line
	float effwidth = std::max(prim.width, 2.0f);

	// determine the bounds of a quad to draw this line
	auto [b0, b1] = render_line_to_quad(prim.bounds, effwidth, effwidth);

	float lx = b1.x1 - b0.x1;
	float ly = b1.y1 - b0.y1;
	float wx = b1.x1 - b1.x0;
	float wy = b1.y1 - b1.y0;
	float line_length = sqrtf(lx * lx + ly * ly);
	float line_width = sqrtf(wx * wx + wy * wy);

	m_vectorbatch[m_batchindex + 0].x = b0.x0;
	m_vectorbatch[m_batchindex + 0].y = b0.y0;
	m_vectorbatch[m_batchindex + 1].x = b0.x1;
	m_vectorbatch[m_batchindex + 1].y = b0.y1;
	m_vectorbatch[m_batchindex + 2].x = b1.x0;
	m_vectorbatch[m_batchindex + 2].y = b1.y0;
	m_vectorbatch[m_batchindex + 3].x = b1.x1;
	m_vectorbatch[m_batchindex + 3].y = b1.y1;

	if (m_shaders->enabled())
	{
		// procedural generated texture
		m_vectorbatch[m_batchindex + 0].u0 = 0.f;
		m_vectorbatch[m_batchindex + 0].v0 = 0.f;
		m_vectorbatch[m_batchindex + 1].u0 = 0.f;
		m_vectorbatch[m_batchindex + 1].v0 = 1.f;
		m_vectorbatch[m_batchindex + 2].u0 = 1.f;
		m_vectorbatch[m_batchindex + 2].v0 = 0.f;
		m_vectorbatch[m_batchindex + 3].u0 = 1.f;
		m_vectorbatch[m_batchindex + 3].v0 = 1.f;
	}
	else
	{
		d3d11_vec2f& start = get_default_texture()->get_uvstart();
		d3d11_vec2f& stop = get_default_texture()->get_uvstop();

		m_vectorbatch[m_batchindex + 0].u0 = start.c.x;
		m_vectorbatch[m_batchindex + 0].v0 = start.c.y;
		m_vectorbatch[m_batchindex + 1].u0 = start.c.x;
		m_vectorbatch[m_batchindex + 1].v0 = stop.c.y;
		m_vectorbatch[m_batchindex + 2].u0 = stop.c.x;
		m_vectorbatch[m_batchindex + 2].v0 = start.c.y;
		m_vectorbatch[m_batchindex + 3].u0 = stop.c.x;
		m_vectorbatch[m_batchindex + 3].v0 = stop.c.y;
	}

	// determine the color of the line
	uint8_t r = (int32_t)(prim.color.r * 255.0f);
	uint8_t g = (int32_t)(prim.color.g * 255.0f);
	uint8_t b = (int32_t)(prim.color.b * 255.0f);
	uint8_t a = (int32_t)(prim.color.a * 255.0f);

	// set the color, Z, and vector parameters
	for (int i = 0; i < 4; i++)
	{
		m_vectorbatch[m_batchindex + i].z = 0.0f;
		m_vectorbatch[m_batchindex + i].r = r;
		m_vectorbatch[m_batchindex + i].g = g;
		m_vectorbatch[m_batchindex + i].b = b;
		m_vectorbatch[m_batchindex + i].a = a;

		// vector length/width
		m_vectorbatch[m_batchindex + i].u1 = line_length;
		m_vectorbatch[m_batchindex + i].v1 = line_width;
	}

	m_batchindex += 4;
}


//============================================================
//  draw_line
//============================================================

void renderer_d3d11::draw_line(const render_primitive &prim)
{
	// get a pointer to the vertex buffer
	d3d11_vertex *vertex = mesh_alloc(4);
	if (vertex == nullptr)
		return;

	// compute the effective width based on the direction of the line
	float effwidth = std::max(prim.width, 1.0f);

	// determine the bounds of a quad to draw this line
	auto [b0, b1] = render_line_to_quad(prim.bounds, effwidth, 0.0f);

	vertex[0].x = b0.x0;
	vertex[0].y = b0.y0;
	vertex[1].x = b0.x1;
	vertex[1].y = b0.y1;
	vertex[2].x = b1.x0;
	vertex[2].y = b1.y0;
	vertex[3].x = b1.x1;
	vertex[3].y = b1.y1;

	d3d11_vec2f& start = get_default_texture()->get_uvstart();
	d3d11_vec2f& stop = get_default_texture()->get_uvstop();

	vertex[0].u0 = start.c.x;
	vertex[0].v0 = start.c.y;
	vertex[1].u0 = start.c.x;
	vertex[1].v0 = stop.c.y;
	vertex[2].u0 = stop.c.x;
	vertex[2].v0 = start.c.y;
	vertex[3].u0 = stop.c.x;
	vertex[3].v0 = stop.c.y;

	// determine the color of the line
	uint8_t r = (uint8_t)std::round(prim.color.r * 255.0f);
	uint8_t g = (uint8_t)std::round(prim.color.g * 255.0f);
	uint8_t b = (uint8_t)std::round(prim.color.b * 255.0f);
	uint8_t a = (uint8_t)std::round(prim.color.a * 255.0f);
	uint32_t color = (a << 24) | (r << 16) | (g << 8) | b;
	//uint8_t r = (uint8_t)(rand() % 256);
	//uint8_t g = (uint8_t)(rand() % 256);
	//uint8_t b = (uint8_t)(rand() % 256);
	//uint8_t a = (uint8_t)(rand() % 256);
	//uint32_t color = rand();

	// set the color and Z parameters
	for (int i = 0; i < 4; i++)
	{
		vertex[i].z = 0.0f;
		vertex[i].r = r;
		vertex[i].g = g;
		vertex[i].b = b;
		vertex[i].a = a;
	}

	// now add a polygon entry
	m_poly[m_numpolys].init(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 2, 6, 4, prim.flags, nullptr, 0.0f, 0.0f, color);
	m_numpolys++;
}


//============================================================
//  draw_quad
//============================================================

void renderer_d3d11::draw_quad(const render_primitive &prim)
{
	d3d11_texture_info *texture = m_texture_manager->find_texinfo(&prim.texture, prim.flags);
	if (texture == nullptr)
		texture = get_default_texture();

	// get a pointer to the vertex buffer
	d3d11_vertex *vertex = mesh_alloc(4);
	if (vertex == nullptr)
		return;

	// fill in the vertexes clockwise
	vertex[0].x = prim.bounds.x0;
	vertex[0].y = prim.bounds.y0;
	vertex[1].x = prim.bounds.x1;
	vertex[1].y = prim.bounds.y0;
	vertex[2].x = prim.bounds.x0;
	vertex[2].y = prim.bounds.y1;
	vertex[3].x = prim.bounds.x1;
	vertex[3].y = prim.bounds.y1;
	float quad_width = prim.get_quad_width();
	float quad_height = prim.get_quad_height();

	// set the texture coordinates
	if (texture != nullptr)
	{
		d3d11_vec2f& start = texture->get_uvstart();
		d3d11_vec2f& stop = texture->get_uvstop();
		d3d11_vec2f delta = stop - start;

		vertex[0].u0 = start.c.x + delta.c.x * prim.texcoords.tl.u;
		vertex[0].v0 = start.c.y + delta.c.y * prim.texcoords.tl.v;
		vertex[1].u0 = start.c.x + delta.c.x * prim.texcoords.tr.u;
		vertex[1].v0 = start.c.y + delta.c.y * prim.texcoords.tr.v;
		vertex[2].u0 = start.c.x + delta.c.x * prim.texcoords.bl.u;
		vertex[2].v0 = start.c.y + delta.c.y * prim.texcoords.bl.v;
		vertex[3].u0 = start.c.x + delta.c.x * prim.texcoords.br.u;
		vertex[3].v0 = start.c.y + delta.c.y * prim.texcoords.br.v;
	}

	// determine the color
	uint8_t r = (uint8_t)std::round(prim.color.r * 255.0f);
	uint8_t g = (uint8_t)std::round(prim.color.g * 255.0f);
	uint8_t b = (uint8_t)std::round(prim.color.b * 255.0f);
	uint8_t a = (uint8_t)std::round(prim.color.a * 255.0f);
	uint32_t color = (a << 24) | (r << 16) | (g << 8) | b;
	//uint8_t r = (uint8_t)(rand() % 256);
	//uint8_t g = (uint8_t)(rand() % 256);
	//uint8_t b = (uint8_t)(rand() % 256);
	//uint8_t a = (uint8_t)(rand() % 256);
	//uint32_t color = rand();

	// set the color and Z parameters
	for (int i = 0; i < 4; i++)
	{
		vertex[i].z = 0.0f;
		vertex[i].r = r;
		vertex[i].g = g;
		vertex[i].b = b;
		vertex[i].a = a;
	}

	// now add a polygon entry
	m_poly[m_numpolys].init(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 2, 6, 4, prim.flags, texture, quad_width, quad_height, color);
	m_numpolys++;
}


//============================================================
//  primitive_alloc
//============================================================

d3d11_vertex *renderer_d3d11::mesh_alloc(int numverts)
{
	// if we're going to overflow, flush
	if (m_numverts + numverts >= VERTEX_BUFFER_LENGTH)
	{
		primitive_flush_pending();
		init_blit_quad();
	}

	// if we have enough room, return a pointer
	if (m_numverts + numverts < VERTEX_BUFFER_LENGTH)
	{
		int oldverts = m_numverts;
		m_numverts += numverts;
		d3d11_vertex *verts = (d3d11_vertex *)m_lockedbuf.pData;
		return &verts[oldverts];
	}

	return nullptr;
}


//============================================================
//  primitive_flush_pending
//============================================================

void renderer_d3d11::primitive_flush_pending()
{
	// unmap the vertex buffer so we can draw with it
	m_d3d11_context->Unmap(m_vertexbuf, 0);

	m_d3d11_context->VSSetShader(m_vs, nullptr, 0);
	m_d3d11_context->VSSetConstantBuffers(0, 1, &m_constant_buffer);

	m_d3d11_context->RSSetViewports(1, &m_viewport);
	m_d3d11_context->RSSetState(m_rasterizer_state);

	m_d3d11_context->PSSetShader(m_ps, nullptr, 0);

	m_d3d11_context->OMSetRenderTargets(1, &m_framebuffer_view, m_depthbuffer_view);
	m_d3d11_context->OMSetDepthStencilState(m_depth_stencil_state, 0);
	m_d3d11_context->OMSetBlendState(nullptr, nullptr, 0xffffffff); // use default blend mode (i.e. disable)

	m_shaders->begin_draw();

	int vertexnum = 4;
	uint32_t stride = sizeof(d3d11_vertex);
	uint32_t offset = 0;
	m_d3d11_context->IASetInputLayout(m_input_layout);
	m_d3d11_context->IASetVertexBuffers(0, 1, &m_vertexbuf, &stride, &offset);
	m_d3d11_context->IASetIndexBuffer(m_indexbuf, DXGI_FORMAT_R32_UINT, 0);

	// now do the polys
	for (int polynum = 0; polynum < m_numpolys; polynum++)
	{
		uint32_t flags = m_poly[polynum].flags();
		d3d11_texture_info *texture = m_poly[polynum].texture();
		bool linear_filter = false;

		// set filtering if different
		if (texture != nullptr)
		{
			if (PRIMFLAG_GET_SCREENTEX(flags))
				linear_filter = (bool)video_config.filter;

			D3D11_TEXTURE_ADDRESS_MODE wrap_mode = PRIMFLAG_GET_TEXWRAP(flags) ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
			//if (m_shaders->enabled())
			//{
				//m_shaders->set_sampler_mode(linear_filter, wrap_mode, m_force_render_states);
			//}
			//else
			{
				set_sampler_mode(0, linear_filter, wrap_mode, m_force_render_states);
			}
		}

		// set the texture if different
		set_texture(texture);

		if (vertexnum >= VERTEX_BUFFER_LENGTH)
			osd_printf_error("Error: vertexnum (%d) is at or over maximum vertex count (%d)\n", vertexnum, VERTEX_BUFFER_LENGTH);

		assert(vertexnum < VERTEX_BUFFER_LENGTH);

		if(m_shaders->enabled())
		{
			m_shaders->render_quad(&m_poly[polynum], vertexnum);
		}
		else
		{
			// Update constants for unshaded drawing
			D3D11_MAPPED_SUBRESOURCE mapped_constants;
			m_d3d11_context->Map(m_constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_constants);
			pipeline_constants* constants = (pipeline_constants *)mapped_constants.pData;
			constants->screen_dims[0] = m_width;
			constants->screen_dims[1] = m_height;
			constants->target_dims[0] = 1.f;
			constants->target_dims[1] = 1.f;
			constants->source_dims[0] = m_last_texture ? m_last_texture->get_rawdims().c.x : 1.f;
			constants->source_dims[1] = m_last_texture ? m_last_texture->get_rawdims().c.y : 1.f;
			m_d3d11_context->Unmap(m_constant_buffer, 0);

			// set blend mode
			set_blendmode(PRIMFLAG_GET_BLENDMODE(flags), m_force_render_states);

			// add the primitives
			m_d3d11_context->IASetPrimitiveTopology(m_poly[polynum].type());
			m_d3d11_context->DrawIndexed(m_poly[polynum].numindices(), 0, vertexnum);
		}

		vertexnum += m_poly[polynum].numverts();
		m_force_render_states = false;
	}

    m_d3d11_context->Map(m_vertexbuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m_lockedbuf);

	m_shaders->end_draw();

	// reset the vertex count
	m_numverts = 0;
	m_numpolys = 0;
}


std::vector<ui::menu_item> renderer_d3d11::get_slider_list()
{
	m_sliders_dirty = false;

	std::vector<ui::menu_item> sliders;
	sliders.insert(sliders.end(), m_sliders.begin(), m_sliders.end());

	if (m_shaders != nullptr && m_shaders->enabled())
	{
		std::vector<ui::menu_item> s_slider = m_shaders->get_slider_list();
		sliders.insert(sliders.end(), s_slider.begin(), s_slider.end());
	}

	return sliders;
}

void renderer_d3d11::set_sliders_dirty()
{
	m_sliders_dirty = true;
}


//============================================================
//  d3d11_texture_info destructor
//============================================================

d3d11_texture_info::~d3d11_texture_info()
{
	if (m_prescaled_view != nullptr && m_prescaled_view != m_view)
	{
		m_prescaled_view->Release();
		m_prescaled_view = nullptr;
	}

	if (m_prescaled_tex != nullptr && m_prescaled_tex != m_tex)
	{
		m_prescaled_tex->Release();
		m_prescaled_tex = nullptr;
	}

	m_view->Release();
	m_tex->Release();
}


//============================================================
//  d3d11_texture_info constructor
//============================================================

d3d11_texture_info::d3d11_texture_info(d3d11_texture_manager &manager, const render_texinfo* texsource, int prescale, uint32_t flags)
	: m_texture_manager(manager)
	, m_renderer(manager.get_renderer())
	, m_hash(manager.texture_compute_hash(texsource, flags))
	, m_flags(flags)
	, m_texinfo(*texsource)
	, m_xprescale(PRIMFLAG_GET_SCREENTEX(flags) ? prescale : 1)
	, m_yprescale(PRIMFLAG_GET_SCREENTEX(flags) ? prescale : 1)
	, m_cur_frame(0)
	, m_tex(nullptr)
	, m_view(nullptr)
	, m_prescaled_tex(nullptr)
	, m_prescaled_view(nullptr)
{
	// set the U/V scale factors
	m_start.c.x = 0.f;
	m_start.c.y = 0.f;
	m_stop.c.x = 1.f;
	m_stop.c.y = 1.f;

	// set the texture width and height
	m_rawdims.c.x = texsource->width;
	m_rawdims.c.y = texsource->height;

	D3D11_TEXTURE2D_DESC    desc;
	memset(&desc, 0, sizeof(desc));
	desc.Width            = texsource->width;
	desc.Height           = texsource->height;
	desc.MipLevels        = 1;
	desc.ArraySize        = 1;
	desc.SampleDesc.Count = 1;
	desc.Usage            = D3D11_USAGE_DYNAMIC;
	desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags   = D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags        = 0;
	m_prescaled_desc = desc;

	D3D11_SUBRESOURCE_DATA  data;
	memset(&data, 0, sizeof(data));
	switch (PRIMFLAG_GET_TEXFORMAT(flags))
	{
		case TEXFORMAT_ARGB32:
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			data.pSysMem = (uint8_t *)texsource->base - texsource->width_margin * 4;
			data.SysMemPitch = texsource->rowpixels * sizeof(uint32_t);
			break;
		case TEXFORMAT_RGB32:
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			data.pSysMem = (uint8_t *)texsource->base - texsource->width_margin * 4;
			data.SysMemPitch = texsource->rowpixels * sizeof(uint32_t);
			break;
		case TEXFORMAT_YUY16:
			desc.Format = DXGI_FORMAT_YUY2;
			data.pSysMem = (uint8_t *)texsource->base - texsource->width_margin * 2;
			data.SysMemPitch = texsource->rowpixels * sizeof(uint16_t);
			break;
		case TEXFORMAT_PALETTE16:
			desc.Format = DXGI_FORMAT_R8G8_UNORM;
			data.pSysMem = (uint8_t *)texsource->base - texsource->width_margin * 2;
			data.SysMemPitch = texsource->rowpixels * sizeof(uint16_t);
			break;
	}
	m_prescaled_data = data;

	HRESULT result;
	ID3D11Device *device = m_renderer.get_device();
	if (!PRIMFLAG_GET_SCREENTEX(flags))
	{
		assert(PRIMFLAG_TEXFORMAT(flags) != TEXFORMAT_YUY16);

		result = device->CreateTexture2D(&desc, &data, &m_tex);
		if (FAILED(result))
		{
			osd_printf_error("Direct3D11: CreateTexture2D failed for non-screen texture: %08x\n", (uint32_t)result);
			goto error;
		}

		result = device->CreateShaderResourceView(m_tex, nullptr, &m_view);
		if (FAILED(result))
		{
			osd_printf_error("Direct3D11: CreateShaderResourceView failed for non-screen texture: %08x\n", (uint32_t)result);
			goto error;
		}

		m_prescaled_tex = m_tex;
		m_prescaled_view = m_view;

		set_data(texsource, flags);
	}
	else
	{
		// screen textures are allocated differently

		// don't prescale above screen size
		const int maxdim = std::max(m_renderer.get_presentation()->BufferDesc.Width, m_renderer.get_presentation()->BufferDesc.Height);

		while (m_xprescale > 1 && m_rawdims.c.x * m_xprescale >= 2 * maxdim)
			m_xprescale--;
		while (m_xprescale > 1 && m_rawdims.c.x * m_xprescale > manager.get_max_texture_width())
			m_xprescale--;

		while (m_yprescale > 1 && m_rawdims.c.y * m_yprescale >= 2 * maxdim)
			m_yprescale--;
		while (m_yprescale > 1 && m_rawdims.c.y * m_yprescale > manager.get_max_texture_height())
			m_yprescale--;

		const int prescale = m_renderer.window().prescale();
		if (m_xprescale != prescale || m_yprescale != prescale)
			osd_printf_verbose("Direct3D: adjusting prescale from %dx%d to %dx%d\n", prescale, prescale, m_xprescale, m_yprescale);

		// allocate something or error out
		result = device->CreateTexture2D(&desc, &data, &m_tex);
		if (FAILED(result))
		{
			osd_printf_error("Direct3D11: CreateTexture2D failed for screen texture: %08x\n", (uint32_t)result);
			goto error;
		}

		result = device->CreateShaderResourceView(m_tex, nullptr, &m_view);
		if (FAILED(result))
		{
			osd_printf_error("Direct3D11: CreateShaderResourceView failed for screen texture: %08x\n", (uint32_t)result);
			goto error;
		}

		m_prescaled_desc.Width *= m_xprescale;
		m_prescaled_desc.Height *= m_yprescale;
	}

	return;

error:
	m_renderer.set_post_fx_unavailable();
	osd_printf_error("Direct3D: Critical warning: A texture failed to allocate.\n");
	m_view = nullptr;//.Reset();
	m_tex = nullptr;//.Reset();
}


//============================================================
//  copyline_palette16
//============================================================

inline void d3d11_texture_info::copyline_palette16(uint32_t *dst, const uint16_t *src, int width, const rgb_t *palette)
{
	for (int x = 0; x < width; x++)
		*dst++ = 0xff000000 | palette[*src++];
}


//============================================================
//  copyline_rgb32
//============================================================

inline void d3d11_texture_info::copyline_rgb32(uint32_t *dst, const uint32_t *src, int width, const rgb_t *palette)
{
	if (palette != nullptr)
	{
		for (int x = 0; x < width; x++)
		{
			const uint32_t val = *src++;
			rgb_t srcpix = val;
			const uint8_t r = (uint8_t)(palette[0x200 + srcpix.r()] >> 16);
			const uint8_t g = (uint8_t)(palette[0x100 + srcpix.g()] >> 8);
			const uint8_t b = (uint8_t)palette[srcpix.b()];
			*dst++ = (r << 24) | (g << 16) | (b << 8) | 0x000000ff;
		}
	}
	else
	{
		memcpy(dst, src, sizeof(uint32_t) * width);
		//for (int x = 0; x < width; x++)
		//{
		//	const uint32_t val = *src++;
		//	const uint32_t r = (uint32_t)((val >> 16) & 0x000000ff);
		//	const uint32_t g = (uint32_t)(val & 0x0000ff00);
		//	const uint32_t b = (uint32_t)((val << 16) & 0x00ff0000);
		//	*dst++ = 0xff000000 | r | g | b;
		//}
	}
}


//============================================================
//  copyline_argb32
//============================================================

inline void d3d11_texture_info::copyline_argb32(uint32_t *dst, const uint32_t *src, int width, const rgb_t *palette)
{
	if (palette != nullptr)
	{
		for (int x = 0; x < width; x++)
		{
			rgb_t srcpix = *src++;
			*dst++ = (srcpix & 0xff000000) | palette[0x200 + srcpix.r()] | palette[0x100 + srcpix.g()] | palette[srcpix.b()];
		}
	}
	else
	{
		for (int x = 0; x < width; x++)
		{
			const uint32_t val = *src++;
			const uint32_t a = val & 0xff000000;//(uint32_t)((val >> 24) & 0x000000ff);
			const uint32_t r = (uint32_t)((val >> 16) & 0x000000ff);
			const uint32_t g = (uint32_t)(val & 0x0000ff00);
			const uint32_t b = (uint32_t)((val << 16) & 0x00ff0000);
			*dst++ = a | r | g | b;
		}
	}
}


//============================================================
//  copyline_yuy16_to_yuy2
//============================================================

inline void d3d11_texture_info::copyline_yuy16_to_yuy2(uint16_t *dst, const uint16_t *src, int width, const rgb_t *palette)
{
	assert(width % 2 == 0);

	if (palette != nullptr) // palette (really RGB map) case
	{
		for (int x = 0; x < width; x += 2)
		{
			uint16_t srcpix0 = *src++;
			uint16_t srcpix1 = *src++;
			*dst++ = palette[0x000 + (srcpix0 >> 8)] | (srcpix0 << 8);
			*dst++ = palette[0x000 + (srcpix1 >> 8)] | (srcpix1 << 8);
		}
	}
	else // direct case
	{
		for (int x = 0; x < width; x += 2)
		{
			uint16_t srcpix0 = *src++;
			uint16_t srcpix1 = *src++;
			*dst++ = (srcpix0 >> 8) | (srcpix0 << 8);
			*dst++ = (srcpix1 >> 8) | (srcpix1 << 8);
		}
	}
}


//============================================================
//  set_data
//============================================================

void d3d11_texture_info::set_data(const render_texinfo *texsource, uint32_t flags)
{
    D3D11_MAPPED_SUBRESOURCE mapped_resource = { 0 };

	// lock the texture
	HRESULT result = m_renderer.get_context()->Map(m_tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource);
	if (FAILED(result))
	{
		osd_printf_error("Direct3D11: Map failed for d3d11_texture_info::set_data: %08x\n", (uint32_t)result);
		return;
	}

	// loop over Y
	int tex_format = PRIMFLAG_GET_TEXFORMAT(flags);
	/*void *dst = mapped_resource.pData;
	switch (tex_format)
	{
		case TEXFORMAT_PALETTE16:
		case TEXFORMAT_YUY16:
			memcpy((uint16_t *)dst, (uint16_t *)texsource->base - texsource->width_margin, sizeof(uint16_t) * texsource->rowpixels * texsource->height);
			break;

		case TEXFORMAT_RGB32:
		case TEXFORMAT_ARGB32:
			memcpy((uint32_t *)dst, (uint32_t *)texsource->base - texsource->width_margin, sizeof(uint16_t) * texsource->rowpixels * texsource->height);
			copyline_rgb32((uint32_t *)dst, (uint32_t *)texsource->base + srcy * texsource->rowpixels, texsource->width, texsource->palette);
			break;

		default:
			osd_printf_error("Unknown texture blendmode=%d format=%d\n", PRIMFLAG_GET_BLENDMODE(flags), PRIMFLAG_GET_TEXFORMAT(flags));
			break;
	}*/

	for (int dsty = 0; dsty < texsource->height; dsty++)
	{
		int srcy = (dsty < 0) ? 0 : (dsty >= texsource->height) ? texsource->height - 1 : dsty;

		void *dst = (BYTE *)mapped_resource.pData + dsty * mapped_resource.RowPitch;

		switch (tex_format)
		{
			case TEXFORMAT_PALETTE16:
				copyline_palette16((uint32_t *)dst, (uint16_t *)texsource->base + srcy * texsource->rowpixels, texsource->width, texsource->palette);
				break;

			case TEXFORMAT_RGB32:
				copyline_rgb32((uint32_t *)dst, (uint32_t *)texsource->base + srcy * texsource->rowpixels, texsource->width, texsource->palette);
				break;

			case TEXFORMAT_ARGB32:
				copyline_argb32((uint32_t *)dst, (uint32_t *)texsource->base + srcy * texsource->rowpixels, texsource->width, texsource->palette);
				break;

			case TEXFORMAT_YUY16:
				copyline_yuy16_to_yuy2((uint16_t *)dst, (uint16_t *)texsource->base + srcy * texsource->rowpixels, texsource->width, texsource->palette);
				break;

			default:
				osd_printf_error("Unknown texture blendmode=%d format=%d\n", PRIMFLAG_GET_BLENDMODE(flags), PRIMFLAG_GET_TEXFORMAT(flags));
				break;
		}
	}

	// unlock
	m_renderer.get_context()->Unmap(m_tex, 0);

	// prescale
	//prescale();
}


//============================================================
//  d3d11_texture_info::prescale
//============================================================

void d3d11_texture_info::prescale()
{
#if 0
	// if we don't need to, just skip it
	if (m_tex == m_prescaled_tex)
	{
		assert(m_xprescale == 1);
		assert(m_yprescale == 1);
		return;
	}

	assert(m_tex);
	assert((m_xprescale > 1) || (m_yprescale > 1));
	HRESULT result;

	// for all cases, we need to get the surface of the render target
	Microsoft::WRL::ComPtr<IDirect3DSurface9> scale_surface;
	result = m_d3dfinaltex->GetSurfaceLevel(0, &scale_surface);
	if (FAILED(result))
		osd_printf_verbose("Direct3D: Error %08lX during texture GetSurfaceLevel call\n", result);

	// first remember the original render target and set the new one
	Microsoft::WRL::ComPtr<IDirect3DSurface9> backbuffer;
	result = m_renderer.get_device()->GetRenderTarget(0, &backbuffer);
	if (FAILED(result))
		osd_printf_verbose("Direct3D: Error %08lX during device GetRenderTarget call\n", result);
	result = m_renderer.get_device()->SetRenderTarget(0, scale_surface);
	if (FAILED(result))
		osd_printf_verbose("Direct3D: Error %08lX during device SetRenderTarget call 1\n", result);
	m_force_render_states = true;

	// start the scene
	result = m_renderer.get_device()->BeginScene();
	if (FAILED(result))
		osd_printf_verbose("Direct3D: Error %08lX during device BeginScene call\n", result);

	// configure the rendering pipeline
	m_renderer.set_sampler_mode(0, false, false);
	m_renderer.set_blendmode(BLENDMODE_NONE);
	result = m_renderer.get_device()->SetTexture(0, m_d3dtex);
	if (FAILED(result))
		osd_printf_verbose("Direct3D: Error %08lX during device SetTexture call\n", result);

	// lock the vertex buffer
	vertex *lockedbuf;
	result = m_renderer.get_vertex_buffer()->Lock(0, 0, (VOID **)&lockedbuf, D3DLOCK_DISCARD);
	if (FAILED(result))
		osd_printf_verbose("Direct3D: Error %08lX during vertex buffer lock call\n", result);

	// configure the X/Y coordinates on the target surface
	lockedbuf[0].x = -0.5f;
	lockedbuf[0].y = -0.5f;
	lockedbuf[1].x = (float)(m_texinfo.width * m_xprescale) - 0.5f;
	lockedbuf[1].y = -0.5f;
	lockedbuf[2].x = -0.5f;
	lockedbuf[2].y = (float)(m_texinfo.height * m_yprescale) - 0.5f;
	lockedbuf[3].x = (float)(m_texinfo.width * m_xprescale) - 0.5f;
	lockedbuf[3].y = (float)(m_texinfo.height * m_yprescale) - 0.5f;

	// configure the U/V coordintes on the source texture
	lockedbuf[0].u0 = 0.0f;
	lockedbuf[0].v0 = 0.0f;
	lockedbuf[1].u0 = (float)m_texinfo.width / m_rawdims.c.x;
	lockedbuf[1].v0 = 0.0f;
	lockedbuf[2].u0 = 0.0f;
	lockedbuf[2].v0 = (float)m_texinfo.height / m_rawdims.c.y;
	lockedbuf[3].u0 = (float)m_texinfo.width / m_rawdims.c.x;
	lockedbuf[3].v0 = (float)m_texinfo.height / m_rawdims.c.y;

	// reset the remaining vertex parameters
	for (int i = 0; i < 4; i++)
	{
		lockedbuf[i].z = 0.0f;
		lockedbuf[i].rhw = 1.0f;
		lockedbuf[i].color = D3DCOLOR_ARGB(0xff,0xff,0xff,0xff);
	}

	// unlock the vertex buffer
	result = m_renderer.get_vertex_buffer()->Unlock();
	if (FAILED(result))
		osd_printf_verbose("Direct3D: Error %08lX during vertex buffer unlock call\n", result);

	// set the stream and draw the triangle strip
	result = m_renderer.get_device()->SetStreamSource(0, m_renderer.get_vertex_buffer(), 0, sizeof(vertex));
	if (FAILED(result))
		osd_printf_verbose("Direct3D: Error %08lX during device SetStreamSource call\n", result);
	result = m_renderer.get_device()->DrawPrimitive(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 0, 2);
	if (FAILED(result))
		osd_printf_verbose("Direct3D: Error %08lX during device DrawPrimitive call\n", result);

	// end the scene
	result = m_renderer.get_device()->EndScene();
	if (FAILED(result))
		osd_printf_verbose("Direct3D: Error %08lX during device end_scene call\n", result);

	// reset the render target and release our reference to the backbuffer
	result = m_renderer.get_device()->SetRenderTarget(0, backbuffer);
	if (FAILED(result))
		osd_printf_verbose("Direct3D: Error %08lX during device SetRenderTarget call 2\n", result);
	backbuffer.Reset();
	m_force_render_states = true;

	// release our reference to the target surface
	scale_surface.Reset();
#endif
}


//============================================================
//  d3d11_render_target::~d3d11_render_target
//============================================================

d3d11_render_target::~d3d11_render_target()
{
}


//============================================================
//  d3d11_render_target::init - initializes a render target
//============================================================

bool d3d11_render_target::init(renderer_d3d11 *renderer, int source_width, int source_height, int target_width, int target_height, int screen_index)
{
	this->target_width = target_width;
	this->target_height = target_height;
	this->screen_index = screen_index;

	width = source_width;
	height = source_height;
	source_dims[0] = float(source_width);
	source_dims[1] = float(source_height);

	D3D11_TEXTURE2D_DESC source_desc = { 0 };
	source_desc.Width            = source_width;
	source_desc.Height           = source_height;
	source_desc.MipLevels        = 1;
	source_desc.ArraySize        = 1;
	source_desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
	source_desc.SampleDesc.Count = 1;
	source_desc.SampleDesc.Quality = 0;
	source_desc.Usage            = D3D11_USAGE_DEFAULT;
	source_desc.BindFlags        = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	source_desc.CPUAccessFlags   = 0;
	source_desc.MiscFlags        = 0;

	D3D11_RENDER_TARGET_VIEW_DESC source_rt_view_desc;
	source_rt_view_desc.Format = source_desc.Format;
	source_rt_view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	source_rt_view_desc.Texture2D.MipSlice = 0;

	D3D11_SHADER_RESOURCE_VIEW_DESC source_shader_view_desc;
	source_shader_view_desc.Format = source_desc.Format;
	source_shader_view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	source_shader_view_desc.Texture2D.MostDetailedMip = 0;
	source_shader_view_desc.Texture2D.MipLevels = 1;

	D3D11_TEXTURE2D_DESC source_depth_desc = { 0 };
	source_depth_desc.Width            = source_width;
	source_depth_desc.Height           = source_height;
	source_depth_desc.MipLevels        = 1;
	source_depth_desc.ArraySize        = 1;
	source_depth_desc.Format           = DXGI_FORMAT_D24_UNORM_S8_UINT;
	source_depth_desc.SampleDesc.Count = 1;
	source_depth_desc.SampleDesc.Quality = 0;
	source_depth_desc.Usage            = D3D11_USAGE_DEFAULT;
	source_depth_desc.BindFlags        = D3D11_BIND_DEPTH_STENCIL;
	source_depth_desc.CPUAccessFlags   = 0;
	source_depth_desc.MiscFlags        = 0;

	D3D11_DEPTH_STENCIL_VIEW_DESC source_depth_rt_view_desc;
	source_depth_rt_view_desc.Format = source_depth_desc.Format;
	source_depth_rt_view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	source_depth_rt_view_desc.Flags = 0;
	source_depth_rt_view_desc.Texture2D.MipSlice = 0;

	D3D11_TEXTURE2D_DESC target_desc = source_desc;
	target_desc.Width  = target_width;
	target_desc.Height = target_height;

	D3D11_RENDER_TARGET_VIEW_DESC target_rt_view_desc;
	target_rt_view_desc.Format = target_desc.Format;
	target_rt_view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	target_rt_view_desc.Texture2D.MipSlice = 0;

	D3D11_SHADER_RESOURCE_VIEW_DESC target_shader_view_desc;
	target_shader_view_desc.Format = target_desc.Format;
	target_shader_view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	target_shader_view_desc.Texture2D.MostDetailedMip = 0;
	target_shader_view_desc.Texture2D.MipLevels = 1;

	D3D11_TEXTURE2D_DESC target_depth_desc = source_depth_desc;
	target_depth_desc.Width  = target_width;
	target_depth_desc.Height = target_height;

	D3D11_DEPTH_STENCIL_VIEW_DESC target_depth_rt_view_desc;
	target_depth_rt_view_desc.Format = target_depth_desc.Format;
	target_depth_rt_view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	target_depth_rt_view_desc.Flags = 0;
	target_depth_rt_view_desc.Texture2D.MipSlice = 0;

	D3D11_RENDER_TARGET_VIEW_DESC cache_rt_view_desc;
	cache_rt_view_desc.Format = target_desc.Format;
	cache_rt_view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	cache_rt_view_desc.Texture2D.MipSlice = 0;

	D3D11_SHADER_RESOURCE_VIEW_DESC cache_shader_view_desc;
	cache_shader_view_desc.Format = target_desc.Format;
	cache_shader_view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	cache_shader_view_desc.Texture2D.MostDetailedMip = 0;
	cache_shader_view_desc.Texture2D.MipLevels = 1;

	D3D11_DEPTH_STENCIL_VIEW_DESC cache_depth_rt_view_desc;
	cache_depth_rt_view_desc.Format = target_depth_desc.Format;
	cache_depth_rt_view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	cache_depth_rt_view_desc.Flags = 0;
	cache_depth_rt_view_desc.Texture2D.MipSlice = 0;

	source_viewport = { 0.f, 0.f, float(source_width), float(source_height), 0.f, 1.f };
	target_viewport = { 0.f, 0.f, float(target_width), float(target_height), 0.f, 1.f };
	cache_viewport = target_viewport;

	for (int rt_index = 0; rt_index < 2; rt_index++)
	{
		if (FAILED(renderer->get_device()->CreateTexture2D(&source_desc, nullptr, &source_texture[rt_index])))
		{
			osd_printf_error("d3d11_render_target::init, CreateTexture2D failed for source_texture %d\n", rt_index);
			return false;
		}

		if (FAILED(renderer->get_device()->CreateRenderTargetView(source_texture[rt_index], &source_rt_view_desc, &source_rt_view[rt_index])))
		{
			osd_printf_error("d3d11_render_target::init, CreateRenderTargetView failed for source_rt_view %d\n", rt_index);
			return false;
		}

		if (FAILED(renderer->get_device()->CreateShaderResourceView(source_texture[rt_index], &source_shader_view_desc, &source_res_view[rt_index])))
		{
			osd_printf_error("d3d11_render_target::init, CreateShaderResourceView failed for source_res_view %d\n", rt_index);
			return false;
		}

		if (FAILED(renderer->get_device()->CreateTexture2D(&source_depth_desc, nullptr, &source_depth_texture[rt_index])))
		{
			osd_printf_error("d3d11_render_target::init, CreateTexture2D failed for source_depth_texture %d\n", rt_index);
			return false;
		}

		if (FAILED(renderer->get_device()->CreateDepthStencilView(source_depth_texture[rt_index], &source_depth_rt_view_desc, &source_depth_rt_view[rt_index])))
		{
			osd_printf_error("d3d11_render_target::init, CreateDepthStencilView failed for source_depth_rt_view %d\n", rt_index);
			return false;
		}

		if (FAILED(renderer->get_device()->CreateTexture2D(&target_desc, nullptr, &target_texture[rt_index])))
		{
			osd_printf_error("d3d11_render_target::init, CreateTexture2D failed for target_texture %d\n", rt_index);
			return false;
		}

		if (FAILED(renderer->get_device()->CreateRenderTargetView(target_texture[rt_index], &target_rt_view_desc, &target_rt_view[rt_index])))
		{
			osd_printf_error("d3d11_render_target::init, CreateRenderTargetView failed for target_rt_view %d\n", rt_index);
			return false;
		}

		if (FAILED(renderer->get_device()->CreateShaderResourceView(target_texture[rt_index], &target_shader_view_desc, &target_res_view[rt_index])))
		{
			osd_printf_error("d3d11_render_target::init, CreateShaderResourceView failed for target_res_view %d\n", rt_index);
			return false;
		}

		if (FAILED(renderer->get_device()->CreateTexture2D(&target_depth_desc, nullptr, &target_depth_texture[rt_index])))
		{
			osd_printf_error("d3d11_render_target::init, CreateTexture2D failed for target_depth_texture %d\n", rt_index);
			return false;
		}

		if (FAILED(renderer->get_device()->CreateDepthStencilView(target_depth_texture[rt_index], &target_depth_rt_view_desc, &target_depth_rt_view[rt_index])))
		{
			osd_printf_error("d3d11_render_target::init, CreateDepthStencilView failed for target_depth_rt_view %d\n", rt_index);
			return false;
		}
	}

	if (FAILED(renderer->get_device()->CreateTexture2D(&target_desc, nullptr, &cache_texture)))
	{
		osd_printf_error("d3d11_render_target::init, CreateTexture2D failed for cache_texture\n");
		return false;
	}

	if (FAILED(renderer->get_device()->CreateRenderTargetView(cache_texture, &cache_rt_view_desc, &cache_rt_view)))
	{
		osd_printf_error("d3d11_render_target::init, CreateRenderTargetView failed for cache_rt_view\n");
		return false;
	}

	if (FAILED(renderer->get_device()->CreateShaderResourceView(cache_texture, &cache_shader_view_desc, &cache_res_view)))
	{
		osd_printf_error("d3d11_render_target::init, CreateShaderResourceView failed for cache_res_view\n");
		return false;
	}

	if (FAILED(renderer->get_device()->CreateTexture2D(&target_depth_desc, nullptr, &cache_depth_texture)))
	{
		osd_printf_error("d3d11_render_target::init, CreateTexture2D failed for cache_depth_texture\n");
		return false;
	}

	if (FAILED(renderer->get_device()->CreateDepthStencilView(cache_depth_texture, &cache_depth_rt_view_desc, &cache_depth_rt_view)))
	{
		osd_printf_error("d3d11_render_target::init, CreateDepthStencilView failed for cache_depth_rt_view\n");
		return false;
	}

	const screen_device *first_screen = screen_device_enumerator(renderer->window().machine().root_device()).first();
	bool vector_screen = first_screen != nullptr && first_screen->screen_type() == SCREEN_TYPE_VECTOR;

	float scale_factor = 0.75f;
	int scale_count = vector_screen ? MAX_BLOOM_COUNT : HALF_BLOOM_COUNT;

	float bloom_width = (float)source_width;
	float bloom_height = (float)source_height;
	float bloom_size = bloom_width < bloom_height ? bloom_width : bloom_height;
	for (int bloom_index = 0; bloom_index < scale_count && bloom_size >= 2.0f; bloom_index++, bloom_size *= scale_factor)
	{
		bloom_dims[bloom_index][0] = (int)bloom_width;
		bloom_dims[bloom_index][1] = (int)bloom_height;

		bloom_viewport[bloom_index] = { 0.f, 0.f, float(int(bloom_width)), float(int(bloom_height)), 0.f, 1.f };

		D3D11_TEXTURE2D_DESC bloom_desc = source_desc;
		bloom_desc.Width  = (uint32_t)bloom_width;
		bloom_desc.Height = (uint32_t)bloom_height;

		D3D11_RENDER_TARGET_VIEW_DESC bloom_rt_view_desc;
		bloom_rt_view_desc.Format = bloom_desc.Format;
		bloom_rt_view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		bloom_rt_view_desc.Texture2D.MipSlice = 0;

		D3D11_SHADER_RESOURCE_VIEW_DESC bloom_shader_view_desc;
    	bloom_shader_view_desc.Format = bloom_desc.Format;
    	bloom_shader_view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    	bloom_shader_view_desc.Texture2D.MostDetailedMip = 0;
    	bloom_shader_view_desc.Texture2D.MipLevels = 1;

		D3D11_TEXTURE2D_DESC bloom_depth_desc = source_depth_desc;
		bloom_depth_desc.Width  = (uint32_t)bloom_width;
		bloom_depth_desc.Height = (uint32_t)bloom_height;

		D3D11_DEPTH_STENCIL_VIEW_DESC bloom_depth_rt_view_desc;
		bloom_depth_rt_view_desc.Format = bloom_depth_desc.Format;
		bloom_depth_rt_view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		bloom_depth_rt_view_desc.Flags = 0;
		bloom_depth_rt_view_desc.Texture2D.MipSlice = 0;

		if (FAILED(renderer->get_device()->CreateTexture2D(&bloom_desc, nullptr, &bloom_texture[bloom_index])))
		{
			osd_printf_error("d3d11_render_target::init, CreateTexture2D failed for bloom_texture[%d] (%dx%d)\n", bloom_index, (int)bloom_width, (int)bloom_height);
			return false;
		}

		if (FAILED(renderer->get_device()->CreateRenderTargetView(bloom_texture[bloom_index], &bloom_rt_view_desc, &bloom_rt_view[bloom_index])))
		{
			osd_printf_error("d3d11_render_target::init, CreateRenderTargetView failed for bloom_rt_view[%d] (%dx%d)\n", bloom_index, (int)bloom_width, (int)bloom_height);
			return false;
		}

		if (FAILED(renderer->get_device()->CreateShaderResourceView(bloom_texture[bloom_index], &bloom_shader_view_desc, &bloom_res_view[bloom_index])))
		{
			osd_printf_error("d3d11_render_target::init, CreateShaderResourceView failed for bloom_res_view[%d] (%dx%d)\n", bloom_index, (int)bloom_width, (int)bloom_height);
			return false;
		}

		if (FAILED(renderer->get_device()->CreateTexture2D(&bloom_depth_desc, nullptr, &bloom_depth_texture[bloom_index])))
		{
			osd_printf_error("d3d11_render_target::init, CreateTexture2D failed for bloom_depth_texture[%d] (%dx%d)\n", bloom_index, (int)bloom_width, (int)bloom_height);
			return false;
		}

		if (FAILED(renderer->get_device()->CreateDepthStencilView(bloom_depth_texture[bloom_index], &bloom_depth_rt_view_desc, &bloom_depth_rt_view[bloom_index])))
		{
			osd_printf_error("d3d11_render_target::init, CreateDepthStencilView failed for bloom_depth_rt_view[%d] (%dx%d)\n", bloom_index, (int)bloom_width, (int)bloom_height);
			return false;
		}

		bloom_width *= scale_factor;
		bloom_height *= scale_factor;

		bloom_count = bloom_index + 1;
	}

	return true;
}

d3d11_texture_info *renderer_d3d11::get_default_texture()
{
	return m_texture_manager->get_default_texture();
}
