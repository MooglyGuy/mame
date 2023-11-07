// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  d3d11hlsl.cpp - Win32 Direct3D11 HLSL implementation
//
//============================================================

// MAME headers
#include "emu.h"

#include "drivenum.h"
#include "emuopts.h"
#include "fileio.h"
#include "main.h"
#include "render.h"
#include "rendlay.h"
#include "rendutil.h"
#include "screen.h"

#include "aviio.h"
#include "png.h"

// MAMEOS headers
#include "winmain.h"
#include "window.h"
#include "modules/render/aviwrite.h"
#include "modules/render/drawd3d11.h"
#include "d3d11comm.h"
#include "strconv.h"
#include "d3d11hlsl.h"
#include "../frontend/mame/ui/slider.h"

#include <array>
#include <locale>
#include <sstream>
#include <utility>


//============================================================
//  PROTOTYPES
//============================================================

static void get_vector(const char *data, int count, float *out, bool report_error);


//============================================================
//  HLSL post-render AVI recorder
//============================================================

class d3d11_movie_recorder
{
public:
	d3d11_movie_recorder(running_machine& machine, renderer_d3d11 *renderer, int width, int height)
		: m_initialized(false)
		, m_renderer(renderer)
		, m_width(width)
		, m_height(height)
		, m_sys_texture(nullptr)
		, m_vid_texture(nullptr)
		, m_vid_target(nullptr)
		, m_vid_depth_texture(nullptr)
		, m_vid_depth_target(nullptr)
	{
		HRESULT result;

		m_avi_writer = std::make_unique<avi_write>(machine, width, height);

		m_frame.allocate(width, height);
		if (!m_frame.valid())
			return;

		D3D11_TEXTURE2D_DESC desc = { 0 };
		desc.Width            = width;
		desc.Height           = height;
		desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.MipLevels        = 1;
		desc.ArraySize        = 1;
		desc.SampleDesc.Count = 1;
		desc.Usage            = D3D11_USAGE_STAGING;
		desc.BindFlags        = 0;
		desc.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;
		desc.MiscFlags        = 0;

		result = m_renderer->get_device()->CreateTexture2D(&desc, nullptr, &m_sys_texture);
		if (FAILED(result))
		{
			osd_printf_error("Direct3D11: Unable to init system-memory texture for HLSL AVI dumping (%08x)\n", (uint32_t)result);
			return;
		}

		desc.Usage          = D3D11_USAGE_DEFAULT;
		desc.BindFlags      = D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;
		result = m_renderer->get_device()->CreateTexture2D(&desc, nullptr, &m_vid_texture);
		if (FAILED(result))
		{
			osd_printf_error("Direct3D11: Unable to init video-memory texture for HLSL AVI dumping (%08x)\n", (uint32_t)result);
			return;
		}

		D3D11_RENDER_TARGET_VIEW_DESC rt_view_desc;
		rt_view_desc.Format = desc.Format;
		rt_view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rt_view_desc.Texture2D.MipSlice = 0;
		result = m_renderer->get_device()->CreateRenderTargetView(m_vid_texture, &rt_view_desc, &m_vid_target);
		if (FAILED(result))
		{
			osd_printf_error("Direct3D11: Unable to init render target view for HLSL AVI dumping (%08x)\n", (uint32_t)result);
			return;
		}

		m_initialized = true;
	}

	~d3d11_movie_recorder()
	{
	}

	void record(const char *name)
	{
		if (!m_initialized)
			return;

		m_avi_writer->record(name);
	}

	void save_frame()
	{
		if (!m_initialized)
			return;

		m_renderer->get_context()->CopyResource(m_sys_texture, m_vid_texture);

		D3D11_MAPPED_SUBRESOURCE resource = { 0 };
		HRESULT result = m_renderer->get_context()->Map(m_sys_texture, 0, D3D11_MAP_READ, 0, &resource);
		if (FAILED(result))
			return;

		for (int y = 0; y < m_height; y++)
		{
			uint32_t *src = (uint32_t *)((uint8_t *)resource.pData + y * resource.RowPitch);
			uint32_t *dst = &m_frame.pix(y);

			for (int x = 0; x < m_width; x++)
			{
				*dst++ = *src++;
			}
		}

		m_renderer->get_context()->Unmap(m_sys_texture, 0);
		m_avi_writer->video_frame(m_frame);
	}

	void add_audio(const int16_t *buffer, int samples_this_frame)
	{
		if (!m_initialized)
			return;

		m_avi_writer->audio_frame(buffer, samples_this_frame);
	}

	ID3D11RenderTargetView * render_target() { return m_vid_target; }
	ID3D11DepthStencilView * depth_target() { return m_vid_depth_target; }

private:
	bool                       m_initialized;

	renderer_d3d11 *           m_renderer;

	std::unique_ptr<avi_write> m_avi_writer;

	bitmap_rgb32               m_frame;
	int                        m_width;
	int                        m_height;
	ID3D11Texture2D *          m_sys_texture;
	ID3D11Texture2D *          m_vid_texture;
	ID3D11RenderTargetView *   m_vid_target;
	ID3D11Texture2D *          m_vid_depth_texture;
	ID3D11DepthStencilView *   m_vid_depth_target;
};


//============================================================
//  shader manager constructor
//============================================================

d3d11_shaders::d3d11_shaders()
	: m_compile_fn(nullptr)
	, m_d3d11(nullptr)
	, m_d3d11_context(nullptr)
	, m_machine(nullptr)
	, m_renderer(nullptr)
	, m_post_fx_enable(false)
	, m_oversampling_enable(false)
	, m_num_screens(0)
	, m_num_targets(0)
	, m_curr_target(0)
	, m_acc_t(0)
	, m_delta_t(0)
	, m_shadow_texture(nullptr)
	, m_lut_texture(nullptr)
	, m_ui_lut_texture(nullptr)
	, m_options(nullptr)
	, m_recording_movie(false)
	, m_render_snap(false)
	, m_snap_width(0)
	, m_snap_height(0)
	, m_initialized(false)
	, m_curr_effect(nullptr)
	, m_diffuse_texture(nullptr)
	, m_curr_texture(nullptr)
	, m_curr_render_target(nullptr)
	, m_curr_poly(nullptr)
{
	std::fill(std::begin(m_target_to_screen), std::end(m_target_to_screen), 0);
}


//============================================================
//  d3d11_shaders destructor
//============================================================

d3d11_shaders::~d3d11_shaders()
{
	if (m_options && (&s_last_options != m_options))
	{
		delete m_options;
		m_options = nullptr;
	}
}


//============================================================
//  d3d11_shaders::save_snapshot
//============================================================

void d3d11_shaders::save_snapshot()
{
	if (!enabled())
		return;

	int width = m_snap_width;
	int height = m_snap_height;
	if (m_renderer->window().swap_xy())
	{
		std::swap(width, height);
	}

	D3D11_TEXTURE2D_DESC desc = { 0 };
	desc.Width            = width;
	desc.Height           = height;
	desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.MipLevels        = 1;
	desc.ArraySize        = 1;
	desc.SampleDesc.Count = 1;
	desc.Usage            = D3D11_USAGE_STAGING;
	desc.BindFlags        = 0;
	desc.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;
	desc.MiscFlags        = 0;

	HRESULT result = m_renderer->get_device()->CreateTexture2D(&desc, nullptr, &m_snap_copy_texture);
	if (FAILED(result))
	{
		osd_printf_verbose("Direct3D: Unable to init system-memory target for HLSL snapshot (%08lX), bailing\n", result);
		return;
	}

	desc.Usage          = D3D11_USAGE_DEFAULT;
	desc.BindFlags      = D3D11_BIND_RENDER_TARGET;
	desc.CPUAccessFlags = 0;
	result = m_renderer->get_device()->CreateTexture2D(&desc, nullptr, &m_snap_texture);
	if (FAILED(result))
	{
		osd_printf_verbose("Direct3D: Unable to init video-memory target for HLSL snapshot (%08lX), bailing\n", result);
		return;
	}

	result = m_renderer->get_device()->CreateShaderResourceView(m_snap_texture, nullptr, &m_snap_view);
	if (FAILED(result))
	{
		osd_printf_error("Direct3D11: Unable to init shader resource view for HLSL AVI dumping (%08x)\n", (uint32_t)result);
		return;
	}

	m_render_snap = true;
}


//============================================================
//  d3d11_shaders::record_movie
//============================================================

void d3d11_shaders::record_movie()
{
	if (!enabled())
		return;

	if (m_recording_movie)
	{
		m_recorder.reset();
		m_recording_movie = false;
		return;
	}

	osd_dim wdim = m_renderer->window().get_size();

	m_recorder = std::make_unique<d3d11_movie_recorder>(*m_machine, m_renderer, wdim.width(), wdim.height());
	m_recorder->record(downcast<windows_options &>(m_machine->options()).d3d_hlsl_write());
	m_recording_movie = true;
}


//============================================================
//  d3d11_shaders::record_audio
//============================================================

void d3d11_shaders::record_audio(const int16_t *buffer, int samples_this_frame)
{
	if (!enabled())
		return;

	if (m_recording_movie)
	{
		m_recorder->add_audio(buffer, samples_this_frame);
	}
}


//============================================================
//  d3d11_shaders::render_snapshot
//============================================================

void d3d11_shaders::render_snapshot(ID3D11Texture2D *texture)
{
	if (!enabled())
		return;

	int width = m_snap_width;
	int height = m_snap_height;
	if (m_renderer->window().swap_xy())
	{
		std::swap(width, height);
	}

	bitmap_rgb32 snapshot(width, height);
	if (!snapshot.valid())
		return;

	// copy the texture
	m_renderer->get_context()->CopyResource(m_snap_copy_texture, texture);

	D3D11_MAPPED_SUBRESOURCE resource = { 0 };
	HRESULT result = m_renderer->get_context()->Map(m_snap_copy_texture, 0, D3D11_MAP_READ, 0, &resource);
	if (FAILED(result))
		return;

	for (int y = 0; y < height; y++)
	{
		uint32_t *src = (uint32_t *)((uint8_t *)resource.pData + y * resource.RowPitch);
		uint32_t *dst = &snapshot.pix(y);

		for (int x = 0; x < width; x++)
		{
			*dst++ = *src++;
		}
	}

	emu_file file(m_machine->options().snapshot_directory(), OPEN_FLAG_WRITE | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_PATHS);
	std::error_condition const filerr = m_machine->video().open_next(file, "png");
	if (filerr)
		return;

	// add two text entries describing the image
	std::string text1 = std::string(emulator_info::get_appname()).append(" ").append(emulator_info::get_build_version());
	std::string text2 = std::string(m_machine->system().manufacturer).append(" ").append(m_machine->system().type.fullname());
	util::png_info pnginfo;
	pnginfo.add_text("Software", text1);
	pnginfo.add_text("System", text2);

	// now do the actual work
	std::error_condition const error = util::png_write_bitmap(file, &pnginfo, snapshot, 1 << 24, nullptr);
	if (error)
		osd_printf_error("Error generating PNG for HLSL snapshot (%s:%d %s)\n", error.category().name(), error.value(), error.message());

	m_renderer->get_context()->Unmap(m_snap_copy_texture, 0);
}


//============================================================
//  remove_render_target - remove an active target
//============================================================

void d3d11_shaders::remove_render_target(int source_width, int source_height, uint32_t screen_index)
{
	remove_render_target(find_render_target(source_width, source_height, screen_index));
}

void d3d11_shaders::remove_render_target(d3d11_render_target *rt)
{
	if (rt == nullptr)
		return;

	for (auto it = m_render_target_list.begin(); it != m_render_target_list.end(); it++)
	{
		if ((*it).get() == rt)
		{
			m_render_target_list.erase(it);
			break;
		}
	}
}


//============================================================
//  d3d11_shaders::set_texture
//============================================================

void d3d11_shaders::set_texture(d3d11_texture_info *texture)
{
	if (!enabled())
	{
		return;
	}

	if (texture)
	{
		m_diffuse_texture = texture;
	}
	else
	{
		m_diffuse_texture = m_renderer->get_default_texture();
	}
}


//============================================================
//  d3d11_shaders::set_filter - indicates if screens should
//  be filtered on upscaling
//============================================================

void d3d11_shaders::set_filter(bool filter_screens)
{
	m_filter_screens = filter_screens;
}


//============================================================
//  d3d11_shaders::init
//============================================================

bool d3d11_shaders::init(ID3D11Device *d3d11, ID3D11DeviceContext *d3d11_context, const d3d_compile_fn compile_fn, running_machine *machine, renderer_d3d11 *renderer)
{
	osd_printf_verbose("Direct3D11: Initialize HLSL\n");

	if (m_initialized)
		return false;

	// check if no driver loaded (not all settings might be loaded yet)
	if (&machine->system() == &GAME_NAME(___empty))
		return false;

	// check if another driver is loaded and reset last options
	if (std::strcmp(machine->system().name, s_last_system_name) != 0)
	{
		strncpy(s_last_system_name, machine->system().name, sizeof(s_last_system_name));

		s_last_options.params_init = false;
	}

	m_compile_fn = compile_fn;
	m_d3d11 = d3d11;
	m_d3d11_context = d3d11_context;
	m_machine = machine;
	m_renderer = renderer;

	auto &winoptions = downcast<windows_options &>(m_machine->options());

	m_post_fx_enable = winoptions.d3d_hlsl_enable();
	m_oversampling_enable = winoptions.d3d_hlsl_oversampling();
	m_snap_width = winoptions.d3d_snap_width();
	m_snap_height = winoptions.d3d_snap_height();

	if (s_last_options.params_init)
	{
		// copy last options if initialized
		osd_printf_verbose("Direct3D11: First restore options\n");
		m_options = &s_last_options;
	}
	else
	{
		// read options if not initialized
		m_options = new d3d11_hlsl_options;
		m_options->params_init = false;

		strncpy(m_options->shadow_mask_texture, winoptions.screen_shadow_mask_texture(), sizeof(m_options->shadow_mask_texture));
		m_options->shadow_mask_tile_mode = winoptions.screen_shadow_mask_tile_mode();
		m_options->shadow_mask_alpha = winoptions.screen_shadow_mask_alpha();
		m_options->shadow_mask_count_x = winoptions.screen_shadow_mask_count_x();
		m_options->shadow_mask_count_y = winoptions.screen_shadow_mask_count_y();
		m_options->shadow_mask_u_size = winoptions.screen_shadow_mask_u_size();
		m_options->shadow_mask_v_size = winoptions.screen_shadow_mask_v_size();
		m_options->shadow_mask_u_offset = winoptions.screen_shadow_mask_u_offset();
		m_options->shadow_mask_v_offset = winoptions.screen_shadow_mask_v_offset();
		m_options->distortion = winoptions.screen_distortion();
		m_options->cubic_distortion = winoptions.screen_cubic_distortion();
		m_options->distort_corner = winoptions.screen_distort_corner();
		m_options->round_corner = winoptions.screen_round_corner();
		m_options->smooth_border = winoptions.screen_smooth_border();
		m_options->reflection = winoptions.screen_reflection();
		m_options->vignetting = winoptions.screen_vignetting();
		m_options->scanline_alpha = winoptions.screen_scanline_amount();
		m_options->scanline_scale = winoptions.screen_scanline_scale();
		m_options->scanline_height = winoptions.screen_scanline_height();
		m_options->scanline_variation = winoptions.screen_scanline_variation();
		m_options->scanline_bright_scale = winoptions.screen_scanline_bright_scale();
		m_options->scanline_bright_offset = winoptions.screen_scanline_bright_offset();
		m_options->scanline_jitter = winoptions.screen_scanline_jitter();
		m_options->hum_bar_alpha = winoptions.screen_hum_bar_alpha();
		get_vector(winoptions.screen_defocus(), 2, m_options->defocus, true);
		get_vector(winoptions.screen_converge_x(), 3, m_options->converge_x, true);
		get_vector(winoptions.screen_converge_y(), 3, m_options->converge_y, true);
		get_vector(winoptions.screen_radial_converge_x(), 3, m_options->radial_converge_x, true);
		get_vector(winoptions.screen_radial_converge_y(), 3, m_options->radial_converge_y, true);
		get_vector(winoptions.screen_red_ratio(), 3, m_options->red_ratio, true);
		get_vector(winoptions.screen_grn_ratio(), 3, m_options->grn_ratio, true);
		get_vector(winoptions.screen_blu_ratio(), 3, m_options->blu_ratio, true);
		get_vector(winoptions.screen_offset(), 3, m_options->offset, true);
		get_vector(winoptions.screen_scale(), 3, m_options->scale, true);
		get_vector(winoptions.screen_power(), 3, m_options->power, true);
		get_vector(winoptions.screen_floor(), 3, m_options->floor, true);
		get_vector(winoptions.screen_phosphor(), 3, m_options->phosphor, true);
		m_options->saturation = winoptions.screen_saturation();
		m_options->chroma_mode = winoptions.screen_chroma_mode();
		get_vector(winoptions.screen_chroma_a(), 2, m_options->chroma_a, true);
		get_vector(winoptions.screen_chroma_b(), 2, m_options->chroma_b, true);
		get_vector(winoptions.screen_chroma_c(), 2, m_options->chroma_c, true);
		get_vector(winoptions.screen_chroma_conversion_gain(), 3, m_options->chroma_conversion_gain, true);
		get_vector(winoptions.screen_chroma_y_gain(), 3, m_options->chroma_y_gain, true);
		m_options->yiq_enable = winoptions.screen_yiq_enable();
		m_options->yiq_jitter = winoptions.screen_yiq_jitter();
		m_options->yiq_cc = winoptions.screen_yiq_cc();
		m_options->yiq_a = winoptions.screen_yiq_a();
		m_options->yiq_b = winoptions.screen_yiq_b();
		m_options->yiq_o = winoptions.screen_yiq_o();
		m_options->yiq_p = winoptions.screen_yiq_p();
		m_options->yiq_n = winoptions.screen_yiq_n();
		m_options->yiq_y = winoptions.screen_yiq_y();
		m_options->yiq_i = winoptions.screen_yiq_i();
		m_options->yiq_q = winoptions.screen_yiq_q();
		m_options->yiq_scan_time = winoptions.screen_yiq_scan_time();
		m_options->yiq_phase_count = winoptions.screen_yiq_phase_count();
		m_options->vector_beam_smooth = winoptions.screen_vector_beam_smooth();
		m_options->vector_length_scale = winoptions.screen_vector_length_scale();
		m_options->vector_length_ratio = winoptions.screen_vector_length_ratio();
		m_options->bloom_blend_mode = winoptions.screen_bloom_blend_mode();
		m_options->bloom_scale = winoptions.screen_bloom_scale();
		get_vector(winoptions.screen_bloom_overdrive(), 3, m_options->bloom_overdrive, true);
		m_options->bloom_level0_weight = winoptions.screen_bloom_lvl0_weight();
		m_options->bloom_level1_weight = winoptions.screen_bloom_lvl1_weight();
		m_options->bloom_level2_weight = winoptions.screen_bloom_lvl2_weight();
		m_options->bloom_level3_weight = winoptions.screen_bloom_lvl3_weight();
		m_options->bloom_level4_weight = winoptions.screen_bloom_lvl4_weight();
		m_options->bloom_level5_weight = winoptions.screen_bloom_lvl5_weight();
		m_options->bloom_level6_weight = winoptions.screen_bloom_lvl6_weight();
		m_options->bloom_level7_weight = winoptions.screen_bloom_lvl7_weight();
		m_options->bloom_level8_weight = winoptions.screen_bloom_lvl8_weight();
		get_vector(winoptions.screen_bloom_shift(), 2, m_options->bloom_shift, true);
		strncpy(m_options->lut_texture, winoptions.screen_lut_texture(), sizeof(m_options->lut_texture));
		m_options->lut_enable = winoptions.screen_lut_enable();
		strncpy(m_options->ui_lut_texture, winoptions.ui_lut_texture(), sizeof(m_options->ui_lut_texture));
		m_options->ui_lut_enable = winoptions.ui_lut_enable();

		m_options->params_init = true;

		osd_printf_verbose("Direct3D11: First store options\n");
		s_last_options = *m_options;
		delete m_options;
		m_options = &s_last_options;
	}

	m_options->params_dirty = true;

	m_initialized = true;

	osd_printf_verbose("Direct3D11: HLSL initialized\n");

	return true;
}


//============================================================
//  d3d11_shaders::begin_frame
//
//  Enumerates the total number of screen textures present.
//
//  Additionally, ensures the presence of necessary post-
//  processing geometry.
//
//============================================================

void d3d11_shaders::begin_frame(render_primitive_list *primlist)
{
	std::fill(std::begin(m_target_to_screen), std::end(m_target_to_screen), 0);
	std::fill(std::begin(m_targets_per_screen), std::end(m_targets_per_screen), 0);
	EQUIVALENT_ARRAY(m_target_to_screen, render_container *) containers;

	// Maximum potential runtime O(max_num_targets^2)
	m_num_targets = 0;
	m_num_screens = 0;
	m_curr_target = 0;
	for (render_primitive &prim : *primlist)
	{
		if (PRIMFLAG_GET_SCREENTEX(prim.flags))
		{
			int screen_index = 0;
			for (; screen_index < m_num_screens && containers[screen_index] != prim.container; screen_index++);
			containers[screen_index] = prim.container;
			m_target_to_screen[m_num_targets] = screen_index;
			m_targets_per_screen[screen_index]++;
			if (screen_index >= m_num_screens)
				m_num_screens++;
			m_num_targets++;
		}
	}

	m_diffuse_texture = m_renderer->get_default_texture();
}


//============================================================
//  d3d11_shaders::end_frame
//
//  Closes out any still-active effects at the end of
//  rendering.
//
//============================================================

void d3d11_shaders::end_frame()
{
	if (m_curr_effect->is_active())
	{
		m_curr_effect->end();
	}
}


//============================================================
//  d3d11_shaders::create_resources
//============================================================

bool d3d11_shaders::create_resources()
{
	if (!m_initialized || !enabled())
		return true;

	if (s_last_options.params_init)
	{
		osd_printf_verbose("Direct3D11: Restore options\n");
		m_options = &s_last_options;
	}

	//HRESULT result = m_renderer->get_device()->GetRenderTarget(0, &backbuffer);
	//if (FAILED(result))
	//{
	//	osd_printf_verbose("Direct3D: Error %08lX during device GetRenderTarget call\n", result);
	//}

	D3D11_TEXTURE2D_DESC desc = { 0 };
	desc.Width            = 4;
	desc.Height           = 4;
	desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.MipLevels        = 1;
	desc.ArraySize        = 1;
	desc.SampleDesc.Count = 1;
	desc.Usage            = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags   = 0;
	desc.MiscFlags        = 0;

	uint32_t black_data[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	D3D11_SUBRESOURCE_DATA data = { 0 };
	data.pSysMem = (uint8_t *)black_data;
	data.SysMemPitch = 4 * sizeof(uint32_t);

	HRESULT result = m_renderer->get_device()->CreateTexture2D(&desc, &data, &m_black_texture);
	if (FAILED(result))
	{
		osd_printf_error("Direct3D11: Unable to init black texture (%08x)\n", (uint32_t)result);
		return false;
	}

	result = m_renderer->get_device()->CreateShaderResourceView(m_black_texture, nullptr, &m_black_view);
	if (FAILED(result))
	{
		osd_printf_error("Direct3D11: Unable to init black texture view (%08x)\n", (uint32_t)result);
		return false;
	}

	//result = m_renderer->get_device()->SetRenderTarget(0, backbuffer.Get());
	//if (FAILED(result))
		//osd_printf_verbose("Direct3D: Error %08lX during device SetRenderTarget call\n", result);

	emu_file file(m_machine->options().art_path(), OPEN_FLAG_READ);
	if (!file.open(m_options->shadow_mask_texture))
	{
		render_load_png(m_shadow_bitmap, file);
		file.close();
	}

	// if we have a shadow bitmap, create a texture for it
	if (m_shadow_bitmap.valid())
	{
		render_texinfo texture = { 0 };

		// fake in the basic data so it looks like it came from render.c
		texture.base = m_shadow_bitmap.raw_pixptr(0);
		texture.rowpixels = m_shadow_bitmap.rowpixels();
		texture.width = m_shadow_bitmap.width();
		texture.height = m_shadow_bitmap.height();
		texture.palette = nullptr;
		texture.seqid = 0;

		// now create it (no prescale, no wrap)
		auto tex = std::make_unique<d3d11_texture_info>(*m_renderer->get_texture_manager(), &texture, 1, PRIMFLAG_BLENDMODE(BLENDMODE_ALPHA) | PRIMFLAG_TEXFORMAT(TEXFORMAT_ARGB32));
		m_shadow_texture = tex.get();
		m_renderer->get_texture_manager()->m_texture_list.push_back(std::move(tex));
	}

	if (!file.open(m_options->lut_texture))
	{
		render_load_png(m_lut_bitmap, file);
		file.close();
	}

	if (m_lut_bitmap.valid())
	{
		render_texinfo texture = { 0 };

		// fake in the basic data so it looks like it came from render.c
		texture.base = m_lut_bitmap.raw_pixptr(0);
		texture.rowpixels = m_lut_bitmap.rowpixels();
		texture.width = m_lut_bitmap.width();
		texture.height = m_lut_bitmap.height();
		texture.palette = nullptr;
		texture.seqid = 0;

		// now create it (no prescale, no wrap)
		std::unique_ptr<d3d11_texture_info> tex = std::make_unique<d3d11_texture_info>(*m_renderer->get_texture_manager(), &texture, 1, PRIMFLAG_BLENDMODE(BLENDMODE_ALPHA) | PRIMFLAG_TEXFORMAT(TEXFORMAT_ARGB32));
		m_lut_texture = tex.get();
		m_renderer->get_texture_manager()->m_texture_list.push_back(std::move(tex));
	}

	if (!file.open(m_options->ui_lut_texture))
	{
		render_load_png(m_ui_lut_bitmap, file);
		file.close();
	}

	if (m_ui_lut_bitmap.valid())
	{
		render_texinfo texture = { 0 };

		// fake in the basic data so it looks like it came from render.c
		texture.base = m_ui_lut_bitmap.raw_pixptr(0);
		texture.rowpixels = m_ui_lut_bitmap.rowpixels();
		texture.width = m_ui_lut_bitmap.width();
		texture.height = m_ui_lut_bitmap.height();
		texture.palette = nullptr;
		texture.seqid = 0;

		// now create it (no prescale, no wrap)
		std::unique_ptr<d3d11_texture_info> tex = std::make_unique<d3d11_texture_info>(*m_renderer->get_texture_manager(), &texture, 1, PRIMFLAG_BLENDMODE(BLENDMODE_ALPHA) | PRIMFLAG_TEXFORMAT(TEXFORMAT_ARGB32));
		m_ui_lut_texture = tex.get();
		m_renderer->get_texture_manager()->m_texture_list.push_back(std::move(tex));
	}

	const char *hlsl_dir = downcast<windows_options &>(m_machine->options()).screen_post_fx_dir();

	m_default_effect        = std::make_unique<d3d11_effect>(this, m_renderer->get_device(), m_renderer->get_context(), m_compile_fn, "default.hlsl", hlsl_dir);
	m_ui_effect             = std::make_unique<d3d11_effect>(this, m_renderer->get_device(), m_renderer->get_context(), m_compile_fn, "primary.hlsl", hlsl_dir);
	m_vector_buffer_effect  = std::make_unique<d3d11_effect>(this, m_renderer->get_device(), m_renderer->get_context(), m_compile_fn, "vector_buffer.hlsl", hlsl_dir);
	m_post_effect           = std::make_unique<d3d11_effect>(this, m_renderer->get_device(), m_renderer->get_context(), m_compile_fn, "post.hlsl", hlsl_dir);
	m_distortion_effect     = std::make_unique<d3d11_effect>(this, m_renderer->get_device(), m_renderer->get_context(), m_compile_fn, "distortion.hlsl", hlsl_dir);
	m_prescale_effect       = std::make_unique<d3d11_effect>(this, m_renderer->get_device(), m_renderer->get_context(), m_compile_fn, "prescale.hlsl", hlsl_dir);
	m_phosphor_effect       = std::make_unique<d3d11_effect>(this, m_renderer->get_device(), m_renderer->get_context(), m_compile_fn, "phosphor.hlsl", hlsl_dir);
	m_focus_effect          = std::make_unique<d3d11_effect>(this, m_renderer->get_device(), m_renderer->get_context(), m_compile_fn, "focus.hlsl", hlsl_dir);
	m_scanline_effect       = std::make_unique<d3d11_effect>(this, m_renderer->get_device(), m_renderer->get_context(), m_compile_fn, "scanline.hlsl", hlsl_dir);
	m_deconverge_effect     = std::make_unique<d3d11_effect>(this, m_renderer->get_device(), m_renderer->get_context(), m_compile_fn, "deconverge.hlsl", hlsl_dir);
	m_color_effect          = std::make_unique<d3d11_effect>(this, m_renderer->get_device(), m_renderer->get_context(), m_compile_fn, "color.hlsl", hlsl_dir);
	m_ntsc_effect           = std::make_unique<d3d11_effect>(this, m_renderer->get_device(), m_renderer->get_context(), m_compile_fn, "ntsc.hlsl", hlsl_dir);
	m_bloom_effect          = std::make_unique<d3d11_effect>(this, m_renderer->get_device(), m_renderer->get_context(), m_compile_fn, "bloom.hlsl", hlsl_dir);
	m_downsample_effect     = std::make_unique<d3d11_effect>(this, m_renderer->get_device(), m_renderer->get_context(), m_compile_fn, "downsample.hlsl", hlsl_dir);
	m_vector_effect         = std::make_unique<d3d11_effect>(this, m_renderer->get_device(), m_renderer->get_context(), m_compile_fn, "vector.hlsl", hlsl_dir);
	m_chroma_effect         = std::make_unique<d3d11_effect>(this, m_renderer->get_device(), m_renderer->get_context(), m_compile_fn, "chroma.hlsl", hlsl_dir);

	std::array<d3d11_effect*, 16> effects = {
			m_default_effect.get(),
			m_ui_effect.get(),
			m_vector_buffer_effect.get(),
			m_post_effect.get(),
			m_distortion_effect.get(),
			m_prescale_effect.get(),
			m_phosphor_effect.get(),
			m_focus_effect.get(),
			m_scanline_effect.get(),
			m_deconverge_effect.get(),
			m_color_effect.get(),
			m_ntsc_effect.get(),
			m_bloom_effect.get(),
			m_downsample_effect.get(),
			m_vector_effect.get(),
			m_chroma_effect.get() };

	for (d3d11_effect *eff : effects)
	{
		if (!eff->is_valid())
			return false;
	}

	m_bloom_effect->add_uniform("ScreenDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SCREEN_DIMS);
	m_bloom_effect->add_uniform("VectorScreen", d3d11_uniform::UT_BOOL, d3d11_uniform::CU_VECTOR_SCREEN);
	m_bloom_effect->add_uniform("Level0Weight", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_BLOOM_LEVEL0_WEIGHT);
	m_bloom_effect->add_uniform("Level1Weight", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_BLOOM_LEVEL1_WEIGHT);
	m_bloom_effect->add_uniform("Level2Weight", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_BLOOM_LEVEL2_WEIGHT);
	m_bloom_effect->add_uniform("Level3Weight", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_BLOOM_LEVEL3_WEIGHT);
	m_bloom_effect->add_uniform("Level4Weight", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_BLOOM_LEVEL4_WEIGHT);
	m_bloom_effect->add_uniform("Level5Weight", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_BLOOM_LEVEL5_WEIGHT);
	m_bloom_effect->add_uniform("Level6Weight", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_BLOOM_LEVEL6_WEIGHT);
	m_bloom_effect->add_uniform("Level7Weight", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_BLOOM_LEVEL7_WEIGHT);
	m_bloom_effect->add_uniform("Level8Weight", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_BLOOM_LEVEL8_WEIGHT);
	m_bloom_effect->add_uniform("BloomBlendMode", d3d11_uniform::UT_INT, d3d11_uniform::CU_BLOOM_BLEND_MODE);
	m_bloom_effect->add_uniform("BloomScale", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_BLOOM_SCALE);
	m_bloom_effect->add_uniform("BloomOverdrive", d3d11_uniform::UT_VEC3, d3d11_uniform::CU_BLOOM_OVERDRIVE);

	m_chroma_effect->add_uniform("ScreenDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SCREEN_DIMS);
	m_chroma_effect->add_uniform("TargetDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_TARGET_DIMS);
	m_chroma_effect->add_uniform("YGain", d3d11_uniform::UT_VEC3, d3d11_uniform::CU_CHROMA_Y_GAIN);
	m_chroma_effect->add_uniform("ChromaA", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_CHROMA_A);
	m_chroma_effect->add_uniform("ChromaB", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_CHROMA_B);
	m_chroma_effect->add_uniform("ChromaC", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_CHROMA_C);

	m_color_effect->add_uniform("ScreenDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SCREEN_DIMS);
	m_color_effect->add_uniform("TargetDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_TARGET_DIMS);
	m_color_effect->add_uniform("PrimTint", d3d11_uniform::UT_VEC3, d3d11_uniform::CU_PRIM_TINT);
	m_color_effect->add_uniform("RedRatios", d3d11_uniform::UT_VEC3, d3d11_uniform::CU_COLOR_RED_RATIOS);
	m_color_effect->add_uniform("GrnRatios", d3d11_uniform::UT_VEC3, d3d11_uniform::CU_COLOR_GRN_RATIOS);
	m_color_effect->add_uniform("BluRatios", d3d11_uniform::UT_VEC3, d3d11_uniform::CU_COLOR_BLU_RATIOS);
	m_color_effect->add_uniform("Offset", d3d11_uniform::UT_VEC3, d3d11_uniform::CU_COLOR_OFFSET);
	m_color_effect->add_uniform("Scale", d3d11_uniform::UT_VEC3, d3d11_uniform::CU_COLOR_SCALE);
	m_color_effect->add_uniform("Saturation", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_COLOR_SATURATION);
	m_color_effect->add_uniform("LutEnable", d3d11_uniform::UT_BOOL, d3d11_uniform::CU_LUT_ENABLE);

	m_deconverge_effect->add_uniform("ScreenDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SCREEN_DIMS);
	m_deconverge_effect->add_uniform("TargetDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_TARGET_DIMS);
	m_deconverge_effect->add_uniform("ConvergeX", d3d11_uniform::UT_VEC3, d3d11_uniform::CU_CONVERGE_LINEAR_X);
	m_deconverge_effect->add_uniform("ConvergeY", d3d11_uniform::UT_VEC3, d3d11_uniform::CU_CONVERGE_LINEAR_Y);
	m_deconverge_effect->add_uniform("RadialConvergeX", d3d11_uniform::UT_VEC3, d3d11_uniform::CU_CONVERGE_RADIAL_X);
	m_deconverge_effect->add_uniform("RadialConvergeY", d3d11_uniform::UT_VEC3, d3d11_uniform::CU_CONVERGE_RADIAL_Y);

	m_default_effect->add_uniform("ScreenDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SCREEN_DIMS);
	m_default_effect->add_uniform("TargetDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_TARGET_DIMS);

	m_distortion_effect->add_uniform("ScreenDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SCREEN_DIMS);
	m_distortion_effect->add_uniform("ScreenCount", d3d11_uniform::UT_INT, d3d11_uniform::CU_SCREEN_COUNT);
	m_distortion_effect->add_uniform("TargetDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_TARGET_DIMS);
	m_distortion_effect->add_uniform("TargetScale", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_TARGET_SCALE);
	m_distortion_effect->add_uniform("QuadDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_QUAD_DIMS);
	m_distortion_effect->add_uniform("DistortionAmount", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_POST_DISTORTION);
	m_distortion_effect->add_uniform("CubicDistortionAmount", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_POST_CUBIC_DISTORTION);
	m_distortion_effect->add_uniform("DistortCornerAmount", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_POST_DISTORT_CORNER);
	m_distortion_effect->add_uniform("RoundCornerAmount", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_POST_ROUND_CORNER);
	m_distortion_effect->add_uniform("SmoothBorderAmount", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_POST_SMOOTH_BORDER);
	m_distortion_effect->add_uniform("VignettingAmount", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_POST_VIGNETTING);
	m_distortion_effect->add_uniform("ReflectionAmount", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_POST_REFLECTION);
	m_distortion_effect->add_uniform("SwapXY", d3d11_uniform::UT_BOOL, d3d11_uniform::CU_SWAP_XY);

	m_downsample_effect->add_uniform("ScreenDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SCREEN_DIMS);
	m_downsample_effect->add_uniform("TargetDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_COUNT); // Don't use the auto-setter for this, we override it in the pass
	m_deconverge_effect->add_uniform("BloomShift", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_COUNT);

	m_focus_effect->add_uniform("ScreenDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SCREEN_DIMS);
	m_focus_effect->add_uniform("TargetDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_TARGET_DIMS);
	m_focus_effect->add_uniform("Defocus", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_FOCUS_SIZE);

	m_ntsc_effect->add_uniform("ScreenDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SCREEN_DIMS);
	m_ntsc_effect->add_uniform("SourceDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SOURCE_DIMS);
	m_ntsc_effect->add_uniform("AValue", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_NTSC_A);
	m_ntsc_effect->add_uniform("BValue", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_NTSC_B);
	m_ntsc_effect->add_uniform("CCValue", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_NTSC_CCFREQ);
	m_ntsc_effect->add_uniform("OValue", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_NTSC_O);
	m_ntsc_effect->add_uniform("PValue", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_NTSC_P);
	m_ntsc_effect->add_uniform("ScanTime", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_NTSC_HTIME);
	m_ntsc_effect->add_uniform("NotchHalfWidth", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_NTSC_NOTCH);
	m_ntsc_effect->add_uniform("YFreqResponse", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_NTSC_YFREQ);
	m_ntsc_effect->add_uniform("IFreqResponse", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_NTSC_IFREQ);
	m_ntsc_effect->add_uniform("QFreqResponse", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_NTSC_QFREQ);
	m_ntsc_effect->add_uniform("SignalOffset", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_NTSC_SIGNAL_OFFSET);

	m_phosphor_effect->add_uniform("ScreenDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SCREEN_DIMS);
	m_phosphor_effect->add_uniform("TargetDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_TARGET_DIMS);
	m_phosphor_effect->add_uniform("Passthrough", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_COUNT);
	m_phosphor_effect->add_uniform("DeltaTime", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_COUNT);
	m_phosphor_effect->add_uniform("Phosphor", d3d11_uniform::UT_VEC3, d3d11_uniform::CU_PHOSPHOR_LIFE);

	m_post_effect->add_uniform("ScreenDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SCREEN_DIMS);
	m_post_effect->add_uniform("SourceDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SOURCE_DIMS);
	m_post_effect->add_uniform("TargetDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_TARGET_DIMS);
	m_post_effect->add_uniform("TargetScale", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_TARGET_SCALE);
	m_post_effect->add_uniform("QuadDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_QUAD_DIMS);
	m_post_effect->add_uniform("ShadowDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_POST_SHADOW_DIMS);
	m_post_effect->add_uniform("ShadowUVOffset", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_POST_SHADOW_UV_OFFSET);
	m_post_effect->add_uniform("SwapXY", d3d11_uniform::UT_BOOL, d3d11_uniform::CU_SWAP_XY);
	m_post_effect->add_uniform("PrepareBloom", d3d11_uniform::UT_BOOL, d3d11_uniform::CU_COUNT);
	m_post_effect->add_uniform("VectorScreen", d3d11_uniform::UT_BOOL, d3d11_uniform::CU_VECTOR_SCREEN);
	m_post_effect->add_uniform("HumBarAlpha", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_POST_HUM_BAR_ALPHA);
	m_post_effect->add_uniform("TimeMilliseconds", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_COUNT);
	m_post_effect->add_uniform("ScreenScale", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_COUNT);
	m_post_effect->add_uniform("ScreenOffset", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_COUNT);
	m_post_effect->add_uniform("BackColor", d3d11_uniform::UT_VEC3, d3d11_uniform::CU_COUNT);
	m_post_effect->add_uniform("ShadowTileMode", d3d11_uniform::UT_INT, d3d11_uniform::CU_POST_SHADOW_TILE_MODE);
	m_post_effect->add_uniform("ShadowAlpha", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_POST_SHADOW_ALPHA);
	m_post_effect->add_uniform("ShadowCount", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_POST_SHADOW_COUNT);
	m_post_effect->add_uniform("ShadowUV", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_POST_SHADOW_UV);
	m_post_effect->add_uniform("Power", d3d11_uniform::UT_VEC3, d3d11_uniform::CU_POST_POWER);
	m_post_effect->add_uniform("Floor", d3d11_uniform::UT_VEC3, d3d11_uniform::CU_POST_FLOOR);
	m_post_effect->add_uniform("ChromaMode", d3d11_uniform::UT_INT, d3d11_uniform::CU_CHROMA_MODE);
	m_post_effect->add_uniform("ConversionGain", d3d11_uniform::UT_VEC3, d3d11_uniform::CU_CHROMA_CONVERSION_GAIN);

	m_prescale_effect->add_uniform("ScreenDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SCREEN_DIMS);
	m_prescale_effect->add_uniform("TargetDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_TARGET_DIMS);
	m_prescale_effect->add_uniform("SourceDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SOURCE_DIMS);

	m_ui_effect->add_uniform("ScreenDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SCREEN_DIMS);
	m_ui_effect->add_uniform("TargetDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_TARGET_DIMS);
	m_ui_effect->add_uniform("UiLutEnable", d3d11_uniform::UT_BOOL, d3d11_uniform::CU_UI_LUT_ENABLE);

	m_scanline_effect->add_uniform("ScreenDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SCREEN_DIMS);
	m_scanline_effect->add_uniform("SourceDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SOURCE_DIMS);
	m_scanline_effect->add_uniform("TargetDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_TARGET_DIMS);
	m_scanline_effect->add_uniform("QuadDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_QUAD_DIMS);
	m_scanline_effect->add_uniform("SwapXY", d3d11_uniform::UT_BOOL, d3d11_uniform::CU_SWAP_XY);
	m_scanline_effect->add_uniform("ScreenScale", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_COUNT);
	m_scanline_effect->add_uniform("ScreenOffset", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_COUNT);
	m_scanline_effect->add_uniform("ScanlineAlpha", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_POST_SCANLINE_ALPHA);
	m_scanline_effect->add_uniform("ScanlineScale", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_POST_SCANLINE_SCALE);
	m_scanline_effect->add_uniform("ScanlineHeight", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_POST_SCANLINE_HEIGHT);
	m_scanline_effect->add_uniform("ScanlineVariation", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_POST_SCANLINE_VARIATION);
	m_scanline_effect->add_uniform("ScanlineOffset", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_POST_SCANLINE_OFFSET);
	m_scanline_effect->add_uniform("ScanlineBrightScale", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_POST_SCANLINE_BRIGHT_SCALE);
	m_scanline_effect->add_uniform("ScanlineBrightOffset", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_POST_SCANLINE_BRIGHT_OFFSET);

	m_vector_buffer_effect->add_uniform("ScreenDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SCREEN_DIMS);
	m_vector_buffer_effect->add_uniform("TargetDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_TARGET_DIMS);
	m_vector_buffer_effect->add_uniform("LutEnable", d3d11_uniform::UT_BOOL, d3d11_uniform::CU_LUT_ENABLE);

	m_vector_effect->add_uniform("ScreenDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_SCREEN_DIMS);
	m_vector_effect->add_uniform("TargetDims", d3d11_uniform::UT_VEC2, d3d11_uniform::CU_TARGET_DIMS);
	m_vector_effect->add_uniform("TimeRatio", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_VECTOR_TIME_RATIO);
	m_vector_effect->add_uniform("TimeScale", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_VECTOR_TIME_SCALE);
	m_vector_effect->add_uniform("LengthRatio", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_VECTOR_LENGTH_RATIO);
	m_vector_effect->add_uniform("LengthScale", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_VECTOR_LENGTH_SCALE);
	m_vector_effect->add_uniform("BeamSmooth", d3d11_uniform::UT_FLOAT, d3d11_uniform::CU_VECTOR_BEAM_SMOOTH);

	for (d3d11_effect *eff : effects)
	{
		eff->finalize_uniforms();
	}

	return true;
}


//============================================================
//  d3d11_shaders::begin_draw
//============================================================

void d3d11_shaders::begin_draw()
{
	if (!enabled())
		return;

	m_curr_target = 0;

	// Update for delta_time
	const double t = m_machine->time().as_double();
	m_delta_t = t - m_acc_t;
	m_acc_t = t;

	set_curr_effect(m_default_effect.get());
}


//============================================================
//  d3d11_shaders::set_curr_effect
//============================================================

void d3d11_shaders::set_curr_effect(d3d11_effect *curr_effect)
{
	if (m_curr_effect == curr_effect)
		return;

	if (m_curr_effect && m_curr_effect->is_active())
	{
		m_curr_effect->end();
	}

	m_curr_effect = curr_effect;
}


//============================================================
//  d3d11_shaders::blit
//============================================================

void d3d11_shaders::blit(int indexcount, int vertnum)
{
	if (!m_curr_effect->is_active())
	{
		m_curr_effect->begin();
	}

	// draw the primitive(s)
	m_renderer->get_context()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	//m_renderer->set_sampler_mode(0, m_filter_screens, D3D11_TEXTURE_ADDRESS_CLAMP, true);
	m_renderer->get_context()->DrawIndexed(indexcount, 0, vertnum);
	m_renderer->get_context()->OMSetRenderTargets(0, nullptr, nullptr);
}

//============================================================
//  d3d11_shaders::find_render_target
//============================================================

d3d11_render_target* d3d11_shaders::find_render_target(int source_width, int source_height, uint32_t screen_index)
{
	for (auto it = m_render_target_list.begin(); it != m_render_target_list.end(); it++)
	{
		if ((*it)->width == source_width &&
			(*it)->height == source_height &&
			(*it)->screen_index == screen_index)
		{
			return (*it).get();
		}
	}

	return nullptr;
}

int d3d11_shaders::ntsc_pass(d3d11_render_target *rt, d3d11_poly_info *poly)
{
	if (!m_options->yiq_enable)
		return 0;

	set_curr_effect(m_ntsc_effect.get());
	m_curr_effect->set_float("SignalOffset", m_curr_texture->get_cur_frame() == 0 ? 0.0f : m_options->yiq_jitter);
	m_curr_effect->update_uniforms(poly, rt->source_rt_view[0], rt->source_depth_rt_view[0]);
	m_curr_effect->set_texture(0, m_diffuse_texture->get_view());

	m_d3d11_context->RSSetViewports(1, &rt->source_viewport);
	m_renderer->set_sampler_mode(0, true, D3D11_TEXTURE_ADDRESS_CLAMP);
	blit();

	return 0;
}

rgb_t d3d11_shaders::apply_color_convolution(rgb_t color)
{
	// this function uses the same algorithm as the color convolution shader pass

	float r = float(color.r()) / 255.0f;
	float g = float(color.g()) / 255.0f;
	float b = float(color.b()) / 255.0f;

	float *rRatio = m_options->red_ratio;
	float *gRatio = m_options->grn_ratio;
	float *bRatio = m_options->blu_ratio;
	float *offset = m_options->offset;
	float *scale = m_options->scale;
	float saturation = m_options->saturation;

	// RGB Tint & Shift
	float rShifted = r * rRatio[0] + g * rRatio[1] + b * rRatio[2];
	float gShifted = r * gRatio[0] + g * gRatio[1] + b * gRatio[2];
	float bShifted = r * bRatio[0] + g * bRatio[1] + b * bRatio[2];

	// RGB Scale & Offset
	r = rShifted * scale[0] + offset[0];
	g = gShifted * scale[1] + offset[1];
	b = bShifted * scale[2] + offset[2];

	// Saturation
	float grayscale[3] = { 0.299f, 0.587f, 0.114f };
	float luma = r * grayscale[0] + g * grayscale[1] + b * grayscale[2];
	float chroma[3] = { r - luma, g - luma, b - luma };

	r = chroma[0] * saturation + luma;
	g = chroma[1] * saturation + luma;
	b = chroma[2] * saturation + luma;

	return rgb_t(std::clamp(int(r * 255.0f), 0, 255), std::clamp(int(g * 255.0f), 0, 255), std::clamp(int(b * 255.0f), 0, 255));
}

int d3d11_shaders::color_convolution_pass(d3d11_render_target *rt, int source_index, d3d11_poly_info *poly)
{
	int next_index = source_index ^ 1;

	set_curr_effect(m_color_effect.get());

	uint32_t tint = (uint32_t)poly->tint();
	float prim_tint[3] = { ((tint >> 16) & 0xff) / 255.0f, ((tint >> 8) & 0xff) / 255.0f, (tint & 0xff) / 255.0f };
	m_curr_effect->set_vector("PrimTint", 3, prim_tint);

	// initial texture is set in shaders::ntsc_pass() if NTSC processing is enabled
	if (!m_options->yiq_enable)
	{
		m_curr_effect->set_bool("SwizzleRGB", true);
		m_curr_effect->update_uniforms(poly, rt->source_rt_view[0], rt->source_depth_rt_view[0]);
		m_curr_effect->set_texture(0, m_diffuse_texture->get_view());
		next_index = 0;
	}
	else
	{
		m_curr_effect->set_bool("SwizzleRGB", false);
		m_curr_effect->update_uniforms(poly, rt->source_rt_view[source_index], rt->source_depth_rt_view[source_index]);
		m_color_effect->set_texture(0, &rt->source_res_view[next_index]);
	}
	if (m_lut_texture)
		m_curr_effect->set_texture(1, m_lut_texture->get_view());

	m_d3d11_context->RSSetViewports(1, &rt->source_viewport);
	m_renderer->set_sampler_mode(0, true, D3D11_TEXTURE_ADDRESS_CLAMP);
	m_renderer->set_sampler_mode(1, true, D3D11_TEXTURE_ADDRESS_CLAMP);
	blit();

	return next_index;
}

int d3d11_shaders::prescale_pass(d3d11_render_target *rt, int source_index, d3d11_poly_info *poly)
{
	set_curr_effect(m_prescale_effect.get());
	m_curr_effect->update_uniforms(poly, rt->target_rt_view[0], rt->target_depth_rt_view[0]);
	m_curr_effect->set_texture(0, &rt->source_res_view[source_index]);
	m_renderer->set_sampler_mode(0, m_filter_screens, D3D11_TEXTURE_ADDRESS_CLAMP, true);

	m_d3d11_context->RSSetViewports(1, &rt->target_viewport);
	blit();

	return 0;
}

int d3d11_shaders::deconverge_pass(d3d11_render_target *rt, int source_index, d3d11_poly_info *poly)
{
	// skip deconverge if no influencing settings
	if (m_options->converge_x[0] == 0.0f && m_options->converge_x[1] == 0.0f && m_options->converge_x[2] == 0.0f &&
		m_options->converge_y[0] == 0.0f && m_options->converge_y[1] == 0.0f && m_options->converge_y[2] == 0.0f &&
		m_options->radial_converge_x[0] == 0.0f && m_options->radial_converge_x[1] == 0.0f && m_options->radial_converge_x[2] == 0.0f &&
		m_options->radial_converge_y[0] == 0.0f && m_options->radial_converge_y[1] == 0.0f && m_options->radial_converge_y[2] == 0.0f)
		return source_index;

	const int next_index = source_index ^ 1;

	set_curr_effect(m_deconverge_effect.get());
	m_curr_effect->update_uniforms(poly, rt->target_rt_view[next_index], rt->target_depth_rt_view[next_index]);
	m_curr_effect->set_texture(0, &rt->target_res_view[source_index]);
	m_renderer->set_sampler_mode(0, true, D3D11_TEXTURE_ADDRESS_CLAMP);

	m_d3d11_context->RSSetViewports(1, &rt->target_viewport);
	blit();

	return next_index;
}

int d3d11_shaders::scanline_pass(d3d11_render_target *rt, int source_index, d3d11_poly_info *poly)
{
	int next_index = source_index ^ 1;

	screen_device_enumerator screen_iterator(m_machine->root_device());
	screen_device *screen = screen_iterator.byindex(m_target_to_screen[m_curr_target]);
	render_container &screen_container = screen->container();
	float xscale = 1.0f / screen_container.xscale();
	float yscale = 1.0f / screen_container.yscale();
	float xoffset = -screen_container.xoffset();
	float yoffset = -screen_container.yoffset();
	float screen_scale[] = { xscale, yscale };
	float screen_offset[] = { xoffset, yoffset };

	set_curr_effect(m_scanline_effect.get());
	m_curr_effect->set_vector("ScreenScale", 2, screen_scale);
	m_curr_effect->set_vector("ScreenOffset", 2, screen_offset);
	m_curr_effect->set_float("ScanlineOffset", m_curr_texture->get_cur_frame() == 0 ? 0.0f : m_options->scanline_jitter);
	m_curr_effect->update_uniforms(poly, rt->target_rt_view[next_index], rt->target_depth_rt_view[next_index]);
	m_curr_effect->set_texture(0, &rt->target_res_view[source_index]);
	m_renderer->set_sampler_mode(0, true, D3D11_TEXTURE_ADDRESS_CLAMP);

	m_d3d11_context->RSSetViewports(1, &rt->target_viewport);
	blit();

	return next_index;
}

int d3d11_shaders::defocus_pass(d3d11_render_target *rt, int source_index, d3d11_poly_info *poly)
{
	// skip defocus if no influencing settings
	if (m_options->defocus[0] == 0.0f && m_options->defocus[1] == 0.0f)
		return source_index;

	int next_index = source_index ^ 1;

	set_curr_effect(m_focus_effect.get());
	m_curr_effect->update_uniforms(poly, rt->target_rt_view[next_index], rt->target_depth_rt_view[next_index]);
	m_curr_effect->set_texture(0, &rt->target_res_view[source_index]);
	m_renderer->set_sampler_mode(0, true, D3D11_TEXTURE_ADDRESS_CLAMP);

	m_d3d11_context->RSSetViewports(1, &rt->target_viewport);
	blit();

	return next_index;
}

int d3d11_shaders::phosphor_pass(d3d11_render_target *rt, int source_index, d3d11_poly_info *poly)
{
	// skip phosphor if no influencing settings
	if (m_options->phosphor[0] == 0.0f && m_options->phosphor[1] == 0.0f && m_options->phosphor[2] == 0.0f)
		return source_index;

	int next_index = source_index ^ 1;

	set_curr_effect(m_phosphor_effect.get());
	m_curr_effect->set_bool("Passthrough", false);
	m_curr_effect->set_float("DeltaTime", delta_time());
	m_curr_effect->update_uniforms(poly, rt->target_rt_view[next_index], rt->target_depth_rt_view[next_index]);
	m_curr_effect->set_texture(0, &rt->target_res_view[source_index]);
	m_renderer->set_sampler_mode(0, true, D3D11_TEXTURE_ADDRESS_CLAMP);
	m_curr_effect->set_texture(1, &rt->cache_res_view);
	m_renderer->set_sampler_mode(1, true, D3D11_TEXTURE_ADDRESS_CLAMP);

	m_d3d11_context->RSSetViewports(1, &rt->target_viewport);
	blit();
	m_curr_effect->end();

	// Pass along our phosphor'd screen
	m_curr_effect->set_bool("Passthrough", true);
	m_curr_effect->update_uniforms(poly, rt->cache_rt_view, rt->cache_depth_rt_view);
	m_curr_effect->set_texture(0, &rt->target_res_view[next_index]);
	m_renderer->set_sampler_mode(0, true, D3D11_TEXTURE_ADDRESS_CLAMP);
	m_curr_effect->set_texture(1, &rt->target_res_view[next_index]);
	m_renderer->set_sampler_mode(1, true, D3D11_TEXTURE_ADDRESS_CLAMP);

	m_d3d11_context->RSSetViewports(1, &rt->cache_viewport);
	blit();

	return next_index;
}

int d3d11_shaders::post_pass(d3d11_render_target *rt, int source_index_guest, int source_index_native, d3d11_poly_info *poly, bool prepare_bloom)
{
	int next_index = 0;

	screen_device_enumerator screen_iterator(m_machine->root_device());
	screen_device *screen = screen_iterator.byindex(m_target_to_screen[m_curr_target]);
	render_container &screen_container = screen->container();

	float xscale = 1.0f / screen_container.xscale();
	float yscale = 1.0f / screen_container.yscale();
	float xoffset = -screen_container.xoffset();
	float yoffset = -screen_container.yoffset();
	float screen_scale[2] = { xscale, yscale };
	float screen_offset[2] = { xoffset, yoffset };

	rgb_t back_color_rgb = screen->has_palette() ? screen->palette().palette()->entry_color(0) : rgb_t(0, 0, 0);
	back_color_rgb = apply_color_convolution(back_color_rgb);
	float back_color[3] = { float(back_color_rgb.r()) / 255.0f, float(back_color_rgb.g()) / 255.0f, float(back_color_rgb.b()) / 255.0f };

	set_curr_effect(m_post_effect.get());
	if (m_shadow_texture)
	{
		m_curr_effect->set_texture(1, m_shadow_texture->get_view());
		m_renderer->set_sampler_mode(1, true, D3D11_TEXTURE_ADDRESS_CLAMP);
	}
	m_curr_effect->set_int("ShadowTileMode", m_options->shadow_mask_tile_mode);
	m_curr_effect->set_vector("BackColor", 3, back_color);
	m_curr_effect->set_vector("ScreenScale", 2, screen_scale);
	m_curr_effect->set_vector("ScreenOffset", 2, screen_offset);
	m_curr_effect->set_float("TimeMilliseconds", (float)m_machine->time().as_double() * 1000.0f);
	m_curr_effect->set_bool("PrepareBloom", prepare_bloom);
	if (prepare_bloom)
	{
		next_index = source_index_guest;
		m_curr_effect->update_uniforms(poly, rt->source_rt_view[next_index], rt->source_depth_rt_view[next_index]);
	}
	else
	{
		next_index = source_index_native ^ 1;
		m_curr_effect->update_uniforms(poly, rt->target_rt_view[next_index], rt->target_depth_rt_view[next_index]);
	}
	m_curr_effect->set_texture(0, &rt->target_res_view[source_index_native]);
	m_renderer->set_sampler_mode(0, true, D3D11_TEXTURE_ADDRESS_CLAMP);

	m_d3d11_context->RSSetViewports(1, prepare_bloom ? &rt->source_viewport : &rt->target_viewport);
	blit();

	return next_index;
}

void d3d11_shaders::downsample_pass(d3d11_render_target *rt, int source_index, d3d11_poly_info *poly)
{
	// skip downsample if no influencing settings
	if (m_options->bloom_scale == 0.0f)
		return;

	set_curr_effect(m_downsample_effect.get());

	for (int bloom_index = 0; bloom_index < rt->bloom_count; bloom_index++)
	{
		m_curr_effect->set_vector("TargetDims", 2, rt->bloom_dims[bloom_index]);
		m_curr_effect->update_uniforms(poly, rt->bloom_rt_view[bloom_index], rt->bloom_depth_rt_view[bloom_index]);
		if (bloom_index == 0)
			m_curr_effect->set_texture(0, &rt->source_res_view[source_index]);
		else
			m_curr_effect->set_texture(0, &rt->bloom_res_view[bloom_index - 1]);
		m_renderer->set_sampler_mode(0, true, D3D11_TEXTURE_ADDRESS_CLAMP);
		m_d3d11_context->RSSetViewports(1, &rt->bloom_viewport[bloom_index]);
		blit();
		m_downsample_effect->end();
	}
}

int d3d11_shaders::chroma_pass(d3d11_render_target *rt, int source_index, d3d11_poly_info *poly)
{
	int next_index = source_index ^ 1;

	set_curr_effect(m_chroma_effect.get());
	m_curr_effect->update_uniforms(poly, rt->target_rt_view[next_index], rt->target_depth_rt_view[next_index]);
	m_curr_effect->set_texture(0, &rt->target_res_view[source_index]);
	m_renderer->set_sampler_mode(0, true, D3D11_TEXTURE_ADDRESS_CLAMP);

	m_d3d11_context->RSSetViewports(1, &rt->target_viewport);
	blit();

	return next_index;
}

int d3d11_shaders::bloom_pass(d3d11_render_target *rt, int source_index, d3d11_poly_info *poly)
{
	// skip bloom if no influencing settings
	if (m_options->bloom_scale == 0.0f)
		return source_index;

	int next_index = source_index ^ 1;

	set_curr_effect(m_bloom_effect.get());

	m_curr_effect->set_float("Level0Weight", m_options->bloom_level0_weight);
	m_curr_effect->set_float("Level1Weight", m_options->bloom_level1_weight);
	m_curr_effect->set_float("Level2Weight", m_options->bloom_level2_weight);
	m_curr_effect->set_float("Level3Weight", m_options->bloom_level3_weight);
	m_curr_effect->set_float("Level4Weight", m_options->bloom_level4_weight);
	m_curr_effect->set_float("Level5Weight", m_options->bloom_level5_weight);
	m_curr_effect->set_float("Level6Weight", m_options->bloom_level6_weight);
	m_curr_effect->set_float("Level7Weight", m_options->bloom_level7_weight);
	m_curr_effect->set_float("Level8Weight", m_options->bloom_level8_weight);

	m_curr_effect->set_int("BloomBlendMode", m_options->bloom_blend_mode);
	m_curr_effect->set_float("BloomScale", m_options->bloom_scale);
	m_curr_effect->set_vector("BloomOverdrive", 3, m_options->bloom_overdrive);

	m_curr_effect->update_uniforms(poly, rt->target_rt_view[next_index], rt->target_depth_rt_view[next_index]);
	m_curr_effect->set_texture(0, &rt->target_res_view[source_index]);
	for (int index = 1; index < rt->bloom_count; index++)
		m_curr_effect->set_texture(index, &rt->bloom_res_view[index - 1]);
	for (int index = rt->bloom_count; index < MAX_BLOOM_COUNT + 1; index++)
		m_curr_effect->set_texture(index, &m_black_view);

	m_d3d11_context->RSSetViewports(1, &rt->target_viewport);
	for (int index = 0; index < MAX_BLOOM_COUNT + 1; index++)
		m_renderer->set_sampler_mode(index, true, D3D11_TEXTURE_ADDRESS_CLAMP);
	blit();

	return next_index;
}

int d3d11_shaders::distortion_pass(d3d11_render_target *rt, int source_index, d3d11_poly_info *poly)
{
	// skip distortion if no influencing settings
	if (m_options->reflection == 0 && m_options->vignetting == 0 && m_options->distortion == 0 && m_options->cubic_distortion == 0 &&
		m_options->distort_corner == 0 && m_options->round_corner == 0 && m_options->smooth_border == 0)
	{
		return source_index;
	}

	int next_index = source_index ^ 1;

	set_curr_effect(m_distortion_effect.get());
	m_curr_effect->update_uniforms(poly, rt->target_rt_view[next_index], rt->target_depth_rt_view[next_index]);
	m_curr_effect->set_texture(0, &rt->target_res_view[source_index]);
	m_renderer->set_sampler_mode(0, true, D3D11_TEXTURE_ADDRESS_CLAMP);

	m_d3d11_context->RSSetViewports(1, &rt->target_viewport);
	blit();

	return next_index;
}

void d3d11_shaders::vector_pass(d3d11_render_target *rt, d3d11_poly_info *poly, int vertnum)
{
	set_curr_effect(m_vector_effect.get());
	float background_color[4] = { 0.f, 0.f, 0.f, 1.f };
	m_d3d11_context->ClearRenderTargetView(rt->target_rt_view[0], background_color);
	m_curr_effect->update_uniforms(poly, rt->target_rt_view[0], rt->target_depth_rt_view[0]);

	// we need to clear the vector render target here
	m_d3d11_context->RSSetViewports(1, &rt->target_viewport);
	m_renderer->set_sampler_mode(0, true, D3D11_TEXTURE_ADDRESS_CLAMP);
	blit(poly->numindices(), vertnum);
}

int d3d11_shaders::vector_buffer_pass(d3d11_render_target *rt, int source_index, d3d11_poly_info *poly)
{
	int next_index = source_index ^ 1;

	set_curr_effect(m_vector_buffer_effect.get());
	m_curr_effect->set_bool("UiLutEnable", false);
	// we need to clear the vector render target here
	float background_color[4] = { 0.f, 0.f, 0.f, 1.f };
	m_d3d11_context->ClearRenderTargetView(rt->target_rt_view[next_index], background_color);
	m_curr_effect->update_uniforms(poly, rt->target_rt_view[next_index], rt->target_depth_rt_view[next_index]);
	m_curr_effect->set_texture(0, &rt->target_res_view[source_index]);
	m_renderer->set_sampler_mode(0, true, D3D11_TEXTURE_ADDRESS_CLAMP);
	if (m_lut_texture)
	{
		m_curr_effect->set_texture(1, m_lut_texture->get_view());
		m_renderer->set_sampler_mode(1, true, D3D11_TEXTURE_ADDRESS_CLAMP);
	}

	m_d3d11_context->RSSetViewports(1, &rt->target_viewport);
	blit();

	return next_index;
}

void d3d11_shaders::screen_pass(d3d11_render_target *rt, int source_index, d3d11_poly_info *poly, int vertnum)
{
	m_renderer->set_blendmode(PRIMFLAG_GET_BLENDMODE(poly->flags()));

	set_curr_effect(m_default_effect.get());
	m_curr_effect->update_uniforms(poly, m_renderer->get_framebuffer(), m_renderer->get_depthbuffer());
	m_curr_effect->set_texture(0, &rt->target_res_view[source_index]);
	m_renderer->set_sampler_mode(0, true, D3D11_TEXTURE_ADDRESS_MIRROR);

	D3D11_VIEWPORT viewport = m_renderer->get_viewport();
	m_d3d11_context->RSSetViewports(1, &viewport);
	blit(poly->numindices(), vertnum);

	if (m_recording_movie)
	{
		m_d3d11_context->RSSetViewports(1, &viewport);
		//blit(m_recorder->render_target(), m_recorder->depth_target());
		m_recorder->save_frame();
	}

	if (m_render_snap)
	{
		// we need to clear the snap render target here
		m_d3d11_context->RSSetViewports(1, &viewport);
		//blit(m_snap_target, nullptr);
		render_snapshot(m_snap_texture);
		m_render_snap = false;
	}
}

void d3d11_shaders::ui_pass(d3d11_poly_info *poly, int vertnum)
{
	m_renderer->set_blendmode(PRIMFLAG_GET_BLENDMODE(poly->flags()));

	set_curr_effect(m_ui_effect.get());
	m_curr_effect->update_uniforms(poly, m_renderer->get_framebuffer(), m_renderer->get_depthbuffer());
	m_curr_effect->set_texture(0, m_diffuse_texture->get_view());
	if (m_ui_lut_texture)
		m_curr_effect->set_texture(1, m_ui_lut_texture->get_view());

	m_renderer->set_sampler_mode(0, true, PRIMFLAG_GET_TEXWRAP(poly->flags()) ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP);
	m_renderer->set_sampler_mode(1, true, D3D11_TEXTURE_ADDRESS_CLAMP);

	D3D11_VIEWPORT viewport = m_renderer->get_viewport();
	m_d3d11_context->RSSetViewports(1, &viewport);
	blit(6, vertnum);
}


//============================================================
//  shaders::render_quad
//============================================================

void d3d11_shaders::render_quad(d3d11_poly_info *poly, int vertnum)
{
	if (!enabled())
		return;

	m_curr_texture = poly->texture();
	m_curr_poly = poly;

	if (PRIMFLAG_GET_SCREENTEX(poly->flags()))
	{
		if (m_curr_texture == nullptr)
		{
			osd_printf_verbose("Direct3D: No texture\n");
			return;
		}

		m_curr_target = m_curr_target < m_num_targets ? m_curr_target : 0;
		m_curr_render_target = find_render_target(m_curr_texture->get_width(), m_curr_texture->get_height(), m_curr_target);

		d3d11_render_target *rt = m_curr_render_target;
		if (rt == nullptr)
		{
			osd_printf_verbose("Direct3D: No raster render target\n");
			return;
		}

		int guest_index = ntsc_pass(rt, poly);
		guest_index = color_convolution_pass(rt, guest_index, poly);

		int native_index = prescale_pass(rt, guest_index, poly);
		native_index = deconverge_pass(rt, native_index, poly);
		native_index = scanline_pass(rt, native_index, poly);
		native_index = defocus_pass(rt, native_index, poly);

		// create bloom textures
		bool bloom_enabled = (m_options->bloom_scale > 0.0f);
		if (bloom_enabled)
		{
			int next_guest_index = post_pass(rt, guest_index, native_index, poly, true);
			downsample_pass(rt, next_guest_index, poly);
		}

		// apply bloom textures (if enabled) and other post effects
		native_index = post_pass(rt, guest_index, native_index, poly, false);
		native_index = bloom_pass(rt, native_index, poly);
		native_index = phosphor_pass(rt, native_index, poly);
		native_index = chroma_pass(rt, native_index, poly);
		native_index = distortion_pass(rt, native_index, poly);

		// render on screen
		screen_pass(rt, native_index, poly, vertnum);
		m_renderer->set_sampler_mode(0, true, PRIMFLAG_GET_TEXWRAP(m_curr_texture->get_flags()) ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP);

		m_curr_texture->increment_frame_count();
		m_curr_texture->mask_frame_count(m_options->yiq_phase_count);

		m_curr_target++;
	}
	else if (PRIMFLAG_GET_VECTOR(poly->flags()))
	{
		m_curr_target = m_curr_target < m_num_targets ? m_curr_target : 0;

		int source_width = int(poly->prim_width() + 0.5f);
		int source_height = int(poly->prim_height() + 0.5f);
		if (m_renderer->window().swap_xy())
		{
			std::swap(source_width, source_height);
		}
		m_curr_render_target = find_render_target(source_width, source_height, m_curr_target);

		d3d11_render_target *rt = m_curr_render_target;
		if (rt == nullptr)
		{
			osd_printf_error("Direct3D11: No vector render target found\n");
			return;
		}

		vector_pass(rt, poly, vertnum);

		m_renderer->get_context()->OMSetRenderTargets(1, m_renderer->get_framebuffer_ptr(), m_renderer->get_depthbuffer());

		m_curr_target++;
	}
	else if (PRIMFLAG_GET_VECTORBUF(poly->flags()))
	{
		m_curr_target = m_curr_target < m_num_targets ? m_curr_target : 0;

		int source_width = int(poly->prim_width() + 0.5f);
		int source_height = int(poly->prim_height() + 0.5f);
		if (m_renderer->window().swap_xy())
		{
			std::swap(source_width, source_height);
		}
		m_curr_render_target = find_render_target(source_width, source_height, m_curr_target);

		d3d11_render_target *rt = m_curr_render_target;
		if (rt == nullptr)
		{
			osd_printf_error("Direct3D11: No vector buffer render target found\n");
			return;
		}

		int native_index = vector_buffer_pass(rt, 0, poly);
		native_index = deconverge_pass(rt, native_index, poly);
		native_index = defocus_pass(rt, native_index, poly);

		// create bloom textures
		int next_guest_index = post_pass(rt, 0, native_index, poly, true);
		downsample_pass(rt, next_guest_index, poly);

		// apply bloom textures
		native_index = post_pass(rt, 0, native_index, poly, false);
		native_index = bloom_pass(rt, native_index, poly);
		native_index = phosphor_pass(rt, native_index, poly);
		native_index = chroma_pass(rt, native_index, poly);
		native_index = distortion_pass(rt, native_index, poly);

		// render on screen
		screen_pass(rt, native_index, poly, vertnum);
		m_renderer->set_sampler_mode(0, true, PRIMFLAG_GET_TEXWRAP(m_curr_texture->get_flags()) ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP);

		m_curr_target++;
	}
	else
	{
		ui_pass(poly, vertnum);
	}

	m_options->params_dirty = false;

	m_curr_render_target = nullptr;
	m_curr_texture = nullptr;
	m_curr_poly = nullptr;
}


//============================================================
//  d3d11_shaders::end_draw
//============================================================

void d3d11_shaders::end_draw()
{
}


//============================================================
//  d3d11_shaders::get_texture_target
//============================================================

d3d11_render_target* d3d11_shaders::get_texture_target(render_primitive *prim, int width, int height, int screen)
{
	if (!enabled())
		return nullptr;

	int source_width = width;
	int source_height = height;
	int source_screen = screen;
	int target_width = int(prim->get_full_quad_width() + 0.5f);
	int target_height = int(prim->get_full_quad_height() + 0.5f);
	target_width *= m_oversampling_enable ? 2 : 1;
	target_height *= m_oversampling_enable ? 2 : 1;
	if (m_renderer->window().swap_xy())
		std::swap(target_width, target_height);

	// find render target and check if the size of the target quad has changed
	d3d11_render_target *target = find_render_target(source_width, source_height, source_screen);
	if (target != nullptr)
	{
		// check if the size of the screen quad has changed
		if (target->target_width != target_width || target->target_height != target_height)
		{
			osd_printf_error("Direct3D11: Get texture target - invalid size\n");
			return nullptr;
		}
	}
	else
	{
		osd_printf_verbose("Direct3D11: Get texture target - not found - %dx%d:%d\n", source_width, source_height, source_screen);
	}

	return target;
}

d3d11_render_target* d3d11_shaders::get_vector_target(render_primitive *prim, int screen)
{
	if (!enabled())
		return nullptr;

	int source_width = int(prim->get_quad_width() + 0.5f);
	int source_height = int(prim->get_quad_height() + 0.5f);
	int source_screen = screen;
	int target_width = int(prim->get_full_quad_width() + 0.5f);
	int target_height = int(prim->get_full_quad_height() + 0.5f);
	target_width *= m_oversampling_enable ? 2 : 1;
	target_height *= m_oversampling_enable ? 2 : 1;
	if (m_renderer->window().swap_xy())
	{
		std::swap(source_width, source_height);
		std::swap(target_width, target_height);
	}

	// find render target
	d3d11_render_target *target = find_render_target(source_width, source_height, source_screen);
	if (target != nullptr)
	{
		// check if the size of the screen quad has changed
		if (target->target_width != target_width || target->target_height != target_height)
		{
			osd_printf_error("Direct3D: Get vector target - invalid size\n");
			return nullptr;
		}
	}
	else
	{
		osd_printf_verbose("Direct3D: Get vector target - not found - %dx%d:%d\n", source_width, source_height, source_screen);
	}

	return target;
}

bool d3d11_shaders::create_vector_target(render_primitive *prim, int screen)
{
	if (!enabled())
		return false;

	int source_width = int(prim->get_quad_width() + 0.5f);
	int source_height = int(prim->get_quad_height() + 0.5f);
	int source_screen = screen;
	int target_width = int(prim->get_full_quad_width() + 0.5f);
	int target_height = int(prim->get_full_quad_height() + 0.5f);
	target_width *= m_oversampling_enable ? 2 : 1;
	target_height *= m_oversampling_enable ? 2 : 1;
	if (m_renderer->window().swap_xy())
	{
		std::swap(source_width, source_height);
		std::swap(target_width, target_height);
	}

	osd_printf_verbose("Direct3D11: Create vector target - %dx%d\n", target_width, target_height);
	if (!add_render_target(prim, source_width, source_height, source_screen, target_width, target_height))
		return false;

	return true;
}


//============================================================
//  d3d11_shaders::add_render_target - register a render
//  target
//============================================================

bool d3d11_shaders::add_render_target(render_primitive *prim, int source_width, int source_height, int source_screen, int target_width, int target_height)
{
	remove_render_target(find_render_target(source_width, source_height, source_screen));

	std::unique_ptr<d3d11_render_target> target = std::make_unique<d3d11_render_target>();

	if (!target->init(m_renderer, source_width, source_height, target_width, target_height, source_screen))
	{
		return false;
	}

	m_render_target_list.push_back(std::move(target));

	return true;
}


//============================================================
//  d3d11_shaders::create_texture_target
//============================================================

bool d3d11_shaders::create_texture_target(render_primitive *prim, int width, int height, int screen)
{
	if (!enabled())
		return false;

	int source_width = width;
	int source_height = height;
	int source_screen = screen;
	int target_width = int(prim->get_full_quad_width() + 0.5f);
	int target_height = int(prim->get_full_quad_height() + 0.5f);
	target_width *= m_oversampling_enable ? 2 : 1;
	target_height *= m_oversampling_enable ? 2 : 1;

	// source texture is already swapped
	if (m_renderer->window().swap_xy())
		std::swap(target_width, target_height);

	osd_printf_verbose("Direct3D11: Create texture target - %dx%d\n", target_width, target_height);
	if (!add_render_target(prim, source_width, source_height, source_screen, target_width, target_height))
	{
		return false;
	}

	return true;
}


//============================================================
//  d3d11_shaders::delete_resources
//============================================================

void d3d11_shaders::delete_resources()
{
	if (!m_initialized || !enabled())
		return;

	m_recording_movie = false;
	m_recorder.reset();

	if (m_options != nullptr)
	{
		osd_printf_verbose("Direct3D11: Store options\n");
		s_last_options = *m_options;
	}

	m_render_target_list.clear();

	m_downsample_effect.reset();
	m_bloom_effect.reset();
	m_vector_effect.reset();
	m_default_effect.reset();
	m_ui_effect.reset();
	m_vector_buffer_effect.reset();
	m_post_effect.reset();
	m_distortion_effect.reset();
	m_prescale_effect.reset();
	m_phosphor_effect.reset();
	m_focus_effect.reset();
	m_scanline_effect.reset();
	m_deconverge_effect.reset();
	m_color_effect.reset();
	m_ntsc_effect.reset();
	m_chroma_effect.reset();

	m_black_texture->Release();
	m_black_texture = nullptr;
	m_black_view->Release();
	m_black_view = nullptr;

	m_shadow_bitmap.reset();
	m_lut_bitmap.reset();
	m_ui_lut_bitmap.reset();
}


//============================================================
//  get_vector
//============================================================

static void get_vector(const char *data, int count, float *out, bool report_error)
{
	std::istringstream is(data);
	is.imbue(std::locale::classic());
	for (int i = 0; count > i; )
	{
		is >> out[i];
		bool bad = !is;
		if (++i < count)
		{
			char ch;
			is >> ch;
			bad = bad || !is || (',' != ch);
		}
		if (bad)
		{
			if (report_error)
				osd_printf_error("Illegal %d-item vector value = %s\n", count, data);
			return;
		}
	}
}


//============================================================
//  d3d11_shaders::slider_alloc - allocate a new slider
//  entry currently duplicated from ui.cpp, this could
//  be done in a more ideal way.
//============================================================

std::unique_ptr<slider_state> d3d11_shaders::slider_alloc(std::string &&title, int32_t minval, int32_t defval, int32_t maxval, int32_t incval, d3d11_slider *arg)
{
	using namespace std::placeholders;
	return std::make_unique<slider_state>(std::move(title), minval, defval, maxval, incval, std::bind(&d3d11_slider::update, arg, _1, _2));
}


//============================================================
//  assorted global slider accessors
//============================================================

enum slider_type
{
	SLIDER_FLOAT,
	SLIDER_INT_ENUM,
	SLIDER_INT,
	SLIDER_COLOR,
	SLIDER_VEC2
};

int32_t d3d11_slider::update(std::string *str, int32_t newval)
{
	switch (m_desc->slider_type)
	{
		case SLIDER_INT_ENUM:
		{
			auto *val_ptr = reinterpret_cast<int32_t *>(m_value);
			if (newval != SLIDER_NOCHANGE)
			{
				*val_ptr = newval;
			}
			if (str != nullptr)
			{
				*str = string_format(m_desc->format, m_desc->strings[*val_ptr]);
			}
			return *val_ptr;
		}

		case SLIDER_INT:
		{
			int *val_ptr = reinterpret_cast<int *>(m_value);
			if (newval != SLIDER_NOCHANGE)
			{
				*val_ptr = newval;
			}
			if (str != nullptr)
			{
				*str = string_format(m_desc->format, *val_ptr);
			}
			return *val_ptr;
		}

		default:
		{
			auto *val_ptr = reinterpret_cast<float *>(m_value);
			if (newval != SLIDER_NOCHANGE)
			{
				*val_ptr = (float)newval * m_desc->scale;
			}
			if (str != nullptr)
			{
				*str = string_format(m_desc->format, *val_ptr);
			}
			return (int32_t)floor(*val_ptr / m_desc->scale + 0.5f);
		}
	}
	return 0;
}

char d3d11_shaders::s_last_system_name[16];

d3d11_hlsl_options d3d11_shaders::s_last_options = { false };

enum slider_option
{
	SLIDER_UI_LUT_ENABLE = 0,
	SLIDER_VECTOR_BEAM_SMOOTH,
	SLIDER_VECTOR_ATT_MAX,
	SLIDER_VECTOR_ATT_LEN_MIN,
	SLIDER_SHADOW_MASK_TILE_MODE,
	SLIDER_SHADOW_MASK_ALPHA,
	SLIDER_SHADOW_MASK_X_COUNT,
	SLIDER_SHADOW_MASK_Y_COUNT,
	SLIDER_SHADOW_MASK_U_SIZE,
	SLIDER_SHADOW_MASK_V_SIZE,
	SLIDER_SHADOW_MASK_U_OFFSET,
	SLIDER_SHADOW_MASK_V_OFFSET,
	SLIDER_DISTORTION,
	SLIDER_CUBIC_DISTORTION,
	SLIDER_DISTORT_CORNER,
	SLIDER_ROUND_CORNER,
	SLIDER_SMOOTH_BORDER,
	SLIDER_REFLECTION,
	SLIDER_VIGNETTING,
	SLIDER_SCANLINE_ALPHA,
	SLIDER_SCANLINE_SCALE,
	SLIDER_SCANLINE_HEIGHT,
	SLIDER_SCANLINE_VARIATION,
	SLIDER_SCANLINE_BRIGHT_SCALE,
	SLIDER_SCANLINE_BRIGHT_OFFSET,
	SLIDER_SCANLINE_JITTER,
	SLIDER_HUM_BAR_ALPHA,
	SLIDER_DEFOCUS,
	SLIDER_CONVERGE_X,
	SLIDER_CONVERGE_Y,
	SLIDER_RADIAL_CONVERGE_X,
	SLIDER_RADIAL_CONVERGE_Y,
	SLIDER_RED_RATIO,
	SLIDER_GREEN_RATIO,
	SLIDER_BLUE_RATIO,
	SLIDER_SATURATION,
	SLIDER_OFFSET,
	SLIDER_SCALE,
	SLIDER_POWER,
	SLIDER_FLOOR,
	SLIDER_CHROMA_MODE,
	SLIDER_CHROMA_A,
	SLIDER_CHROMA_B,
	SLIDER_CHROMA_C,
	SLIDER_CHROMA_CONVERSION_GAIN,
	SLIDER_Y_GAIN,
	SLIDER_PHOSPHOR,
	SLIDER_BLOOM_BLEND_MODE,
	SLIDER_BLOOM_SCALE,
	SLIDER_BLOOM_OVERDRIVE,
	SLIDER_BLOOM_LVL0_SCALE,
	SLIDER_BLOOM_LVL1_SCALE,
	SLIDER_BLOOM_LVL2_SCALE,
	SLIDER_BLOOM_LVL3_SCALE,
	SLIDER_BLOOM_LVL4_SCALE,
	SLIDER_BLOOM_LVL5_SCALE,
	SLIDER_BLOOM_LVL6_SCALE,
	SLIDER_BLOOM_LVL7_SCALE,
	SLIDER_BLOOM_LVL8_SCALE,
	SLIDER_NTSC_ENABLE,
	SLIDER_NTSC_JITTER,
	SLIDER_NTSC_A_VALUE,
	SLIDER_NTSC_B_VALUE,
	SLIDER_NTSC_P_VALUE,
	SLIDER_NTSC_O_VALUE,
	SLIDER_NTSC_CC_VALUE,
	SLIDER_NTSC_N_VALUE,
	SLIDER_NTSC_Y_VALUE,
	SLIDER_NTSC_I_VALUE,
	SLIDER_NTSC_Q_VALUE,
	SLIDER_NTSC_SCAN_TIME,
	SLIDER_LUT_ENABLE,
	SLIDER_BLOOM_SHIFT,
};

enum slider_screen_type
{
	SLIDER_SCREEN_TYPE_NONE = 0,
	SLIDER_SCREEN_TYPE_RASTER = 1,
	SLIDER_SCREEN_TYPE_VECTOR = 2,
	SLIDER_SCREEN_TYPE_LCD = 4,
	SLIDER_SCREEN_TYPE_LCD_OR_RASTER = SLIDER_SCREEN_TYPE_RASTER | SLIDER_SCREEN_TYPE_LCD,
	SLIDER_SCREEN_TYPE_ANY = SLIDER_SCREEN_TYPE_RASTER | SLIDER_SCREEN_TYPE_VECTOR | SLIDER_SCREEN_TYPE_LCD
};

d3d11_slider_desc d3d11_shaders::s_sliders[] =
{
	{ "3D LUT (UI/Artwork)",                0,     0,     1, 1, SLIDER_INT_ENUM, SLIDER_SCREEN_TYPE_ANY,           SLIDER_UI_LUT_ENABLE,           0,        "%s",    { "Off", "On" } },
	{ "Vector Beam Smooth Amount",          0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_VECTOR,        SLIDER_VECTOR_BEAM_SMOOTH,      0.01f,    "%1.2f", {} },
	{ "Vector Attenuation Maximum",         0,    50,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_VECTOR,        SLIDER_VECTOR_ATT_MAX,          0.01f,    "%1.2f", {} },
	{ "Vector Attenuation Length Minimum",  1,   500,  1000, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_VECTOR,        SLIDER_VECTOR_ATT_LEN_MIN,      0.001f,   "%1.3f", {} },
	{ "Shadow Mask Tile Mode",              0,     0,     1, 1, SLIDER_INT_ENUM, SLIDER_SCREEN_TYPE_ANY,           SLIDER_SHADOW_MASK_TILE_MODE,   0,        "%s",    { "Screen", "Source" } },
	{ "Shadow Mask Amount",                 0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_SHADOW_MASK_ALPHA,       0.01f,    "%1.2f", {} },
	{ "Shadow Mask Pixel X Count",          1,     1,  1024, 1, SLIDER_INT,      SLIDER_SCREEN_TYPE_ANY,           SLIDER_SHADOW_MASK_X_COUNT,     0,        "%d",    {} },
	{ "Shadow Mask Pixel Y Count",          1,     1,  1024, 1, SLIDER_INT,      SLIDER_SCREEN_TYPE_ANY,           SLIDER_SHADOW_MASK_Y_COUNT,     0,        "%d",    {} },
	{ "Shadow Mask U Size",                 1,     1,    32, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_SHADOW_MASK_U_SIZE,      0.03125f, "%2.5f", {} },
	{ "Shadow Mask V Size",                 1,     1,    32, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_SHADOW_MASK_V_SIZE,      0.03125f, "%2.5f", {} },
	{ "Shadow Mask U Offset",            -100,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_SHADOW_MASK_U_OFFSET,    0.01f,    "%1.2f", {} },
	{ "Shadow Mask V Offset",            -100,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_SHADOW_MASK_V_OFFSET,    0.01f,    "%1.2f", {} },
	{ "Quadric Distortion Amount",       -200,     0,   200, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_DISTORTION,              0.01f,    "%2.2f", {} },
	{ "Cubic Distortion Amount",         -200,     0,   200, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_CUBIC_DISTORTION,        0.01f,    "%2.2f", {} },
	{ "Distorted Corner Amount",            0,     0,   200, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_DISTORT_CORNER,          0.01f,    "%1.2f", {} },
	{ "Rounded Corner Amount",              0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_ROUND_CORNER,            0.01f,    "%1.2f", {} },
	{ "Smooth Border Amount",               0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_SMOOTH_BORDER,           0.01f,    "%1.2f", {} },
	{ "Reflection Amount",                  0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_REFLECTION,              0.01f,    "%1.2f", {} },
	{ "Vignetting Amount",                  0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_VIGNETTING,              0.01f,    "%1.2f", {} },
	{ "Scanline Amount",                    0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_SCANLINE_ALPHA,          0.01f,    "%1.2f", {} },
	{ "Overall Scanline Scale",             0,   100,   400, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_SCANLINE_SCALE,          0.01f,    "%1.2f", {} },
	{ "Individual Scanline Scale",          0,   100,   400, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_SCANLINE_HEIGHT,         0.01f,    "%1.2f", {} },
	{ "Scanline Variation",                 0,   100,   400, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_SCANLINE_VARIATION,      0.01f,    "%1.2f", {} },
	{ "Scanline Brightness Scale",          0,   100,   200, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_SCANLINE_BRIGHT_SCALE,   0.01f,    "%1.2f", {} },
	{ "Scanline Brightness Offset",         0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_SCANLINE_BRIGHT_OFFSET,  0.01f,    "%1.2f", {} },
	{ "Scanline Jitter Amount",             0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_SCANLINE_JITTER,         0.01f,    "%1.2f", {} },
	{ "Hum Bar Amount",                     0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_HUM_BAR_ALPHA,           0.01f,    "%2.2f", {} },
	{ "Defocus",                            0,     0,    20, 1, SLIDER_VEC2,     SLIDER_SCREEN_TYPE_ANY,           SLIDER_DEFOCUS,                 0.1f,     "%1.1f", {} },
	{ "Linear Convergence X,",           -100,     0,   100, 1, SLIDER_COLOR,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_CONVERGE_X,              0.1f,     "%3.1f",{} },
	{ "Linear Convergence Y,",           -100,     0,   100, 1, SLIDER_COLOR,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_CONVERGE_Y,              0.1f,     "%3.1f", {} },
	{ "Radial Convergence X,",           -100,     0,   100, 1, SLIDER_COLOR,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_RADIAL_CONVERGE_X,       0.1f,     "%3.1f", {} },
	{ "Radial Convergence Y,",           -100,     0,   100, 1, SLIDER_COLOR,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_RADIAL_CONVERGE_Y,       0.1f,     "%3.1f", {} },
	{ "Red Output from",                 -400,     0,   400, 5, SLIDER_COLOR,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_RED_RATIO,               0.005f,   "%2.3f", {} },
	{ "Green Output from",               -400,     0,   400, 5, SLIDER_COLOR,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_GREEN_RATIO,             0.005f,   "%2.3f", {} },
	{ "Blue Output from",                -400,     0,   400, 5, SLIDER_COLOR,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_BLUE_RATIO,              0.005f,   "%2.3f", {} },
	{ "Color Saturation",                   0,  1000,  4000, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_SATURATION,              0.01f,    "%2.2f", {} },
	{ "Signal Offset,",                  -100,     0,   100, 1, SLIDER_COLOR,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_OFFSET,                  0.01f,    "%2.2f", {} },
	{ "Signal Scale,",                   -200,   100,   200, 1, SLIDER_COLOR,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_SCALE,                   0.01f,    "%2.2f", {} },
	{ "Signal Exponent,",                -800,     0,   800, 1, SLIDER_COLOR,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_POWER,                   0.01f,    "%2.2f", {} },
	{ "Signal Floor,",                      0,     0,   100, 1, SLIDER_COLOR,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_FLOOR,                   0.01f,    "%2.2f", {} },
	{ "Color Mode,",                        1,     3,     3, 1, SLIDER_INT_ENUM, SLIDER_SCREEN_TYPE_ANY,           SLIDER_CHROMA_MODE,             0,        "%s", { "", "Monochrome", "Dichrome", "Trichrome" } },
	{ "Chroma Conversion Gain,",            0,     0, 10000,10, SLIDER_COLOR,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_CHROMA_CONVERSION_GAIN,  0.0001f,  "%1.4f", {} },
	{ "Phosphor A Chromaticity,",           0,     0,  1000,10, SLIDER_VEC2,     SLIDER_SCREEN_TYPE_ANY,           SLIDER_CHROMA_A,                0.001f,   "%1.3f", {} },
	{ "Phosphor B Chromaticity,",           0,     0,  1000,10, SLIDER_VEC2,     SLIDER_SCREEN_TYPE_ANY,           SLIDER_CHROMA_B,                0.001f,   "%1.3f", {} },
	{ "Phosphor C Chromaticity,",           0,     0,  1000,10, SLIDER_VEC2,     SLIDER_SCREEN_TYPE_ANY,           SLIDER_CHROMA_C,                0.001f,   "%1.3f", {} },
	{ "Phosphor Gain,",                     0,     0, 10000,10, SLIDER_COLOR,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_Y_GAIN,                  0.0001f,  "%1.4f", {} },
	{ "Phosphor Persistence,",              0,     0,   100, 1, SLIDER_COLOR,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_PHOSPHOR,                0.01f,    "%2.2f", {} },
	{ "Bloom Blend Mode",                   0,     0,     1, 1, SLIDER_INT_ENUM, SLIDER_SCREEN_TYPE_ANY,           SLIDER_BLOOM_BLEND_MODE,        0,        "%s",    { "Brighten", "Darken" } },
	{ "Bloom Scale",                        0,     0,  2000, 5, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_BLOOM_SCALE,             0.001f,   "%1.3f", {} },
	{ "Bloom Overdrive,",                   0,     0,  2000, 5, SLIDER_COLOR,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_BLOOM_OVERDRIVE,         0.001f,   "%1.3f", {} },
	{ "Bloom Level 0 Scale",                0,   100,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_BLOOM_LVL0_SCALE,        0.01f,    "%1.2f", {} },
	{ "Bloom Level 1 Scale",                0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_BLOOM_LVL1_SCALE,        0.01f,    "%1.2f", {} },
	{ "Bloom Level 2 Scale",                0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_BLOOM_LVL2_SCALE,        0.01f,    "%1.2f", {} },
	{ "Bloom Level 3 Scale",                0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_BLOOM_LVL3_SCALE,        0.01f,    "%1.2f", {} },
	{ "Bloom Level 4 Scale",                0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_BLOOM_LVL4_SCALE,        0.01f,    "%1.2f", {} },
	{ "Bloom Level 5 Scale",                0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_BLOOM_LVL5_SCALE,        0.01f,    "%1.2f", {} },
	{ "Bloom Level 6 Scale",                0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_BLOOM_LVL6_SCALE,        0.01f,    "%1.2f", {} },
	{ "Bloom Level 7 Scale",                0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_BLOOM_LVL7_SCALE,        0.01f,    "%1.2f", {} },
	{ "Bloom Level 8 Scale",                0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_ANY,           SLIDER_BLOOM_LVL8_SCALE,        0.01f,    "%1.2f", {} },
	{ "NTSC Processing",                    0,     0,     1, 1, SLIDER_INT_ENUM, SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_NTSC_ENABLE,             0,        "%s",    { "Off", "On" } },
	{ "NTSC Frame Jitter Offset",           0,     0,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_NTSC_JITTER,             0.01f,    "%1.2f", {} },
	{ "NTSC A Value",                    -100,    50,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_NTSC_A_VALUE,            0.01f,    "%1.2f", {} },
	{ "NTSC B Value",                    -100,    50,   100, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_NTSC_B_VALUE,            0.01f,    "%1.2f", {} },
	{ "NTSC Incoming Phase Pixel Clock Scale",-300,100, 300, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_NTSC_P_VALUE,            0.01f,    "%1.2f", {} },
	{ "NTSC Outgoing Phase Offset (radians)",-314, 0,   314, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_NTSC_O_VALUE,            0.01f,    "%1.2f", {} },
	{ "NTSC Color Carrier (MHz)",           0, 31500,6*8800, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_NTSC_CC_VALUE,           1/8800.f, "%1.5f", {} },
	{ "NTSC Color Notch Filter Width (MHz)",0,   100,   600, 5, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_NTSC_N_VALUE,            0.01f,    "%1.2f", {} },
	{ "NTSC Y Signal Bandwidth (MHz)",      0,   600,  2100, 5, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_NTSC_Y_VALUE,            0.01f,    "%1.2f", {} },
	{ "NTSC I Signal Bandwidth (MHz)",      0,   120,  2100, 5, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_NTSC_I_VALUE,            0.01f,    "%1.2f", {} },
	{ "NTSC Q Signal Bandwidth (MHz)",      0,    60,  2100, 5, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_NTSC_Q_VALUE,            0.01f,    "%1.2f", {} },
	{ "NTSC Scanline Duration (uSec)",      0,  5260, 10000, 1, SLIDER_FLOAT,    SLIDER_SCREEN_TYPE_LCD_OR_RASTER, SLIDER_NTSC_SCAN_TIME,          0.01f,    "%1.2f", {} },
	{ "3D LUT (Screen)",                    0,     0,     1, 1, SLIDER_INT_ENUM, SLIDER_SCREEN_TYPE_ANY,           SLIDER_LUT_ENABLE,              0,        "%s",    { "Off", "On" } },
	{ "Bloom Shift",                     -100,     0,   100, 1, SLIDER_VEC2,     SLIDER_SCREEN_TYPE_VECTOR,        SLIDER_BLOOM_SHIFT,             0.01f,    "%1.2f", {} },
	{ nullptr, 0, 0, 0, 0, 0, 0, -1, 0, nullptr, {} }
};


void *d3d11_shaders::get_slider_option(int id, int index)
{
	switch (id)
	{
		case SLIDER_VECTOR_BEAM_SMOOTH: return &(m_options->vector_beam_smooth);
		case SLIDER_VECTOR_ATT_MAX: return &(m_options->vector_length_scale);
		case SLIDER_VECTOR_ATT_LEN_MIN: return &(m_options->vector_length_ratio);
		case SLIDER_SHADOW_MASK_TILE_MODE: return &(m_options->shadow_mask_tile_mode);
		case SLIDER_SHADOW_MASK_ALPHA: return &(m_options->shadow_mask_alpha);
		case SLIDER_SHADOW_MASK_X_COUNT: return &(m_options->shadow_mask_count_x);
		case SLIDER_SHADOW_MASK_Y_COUNT: return &(m_options->shadow_mask_count_y);
		case SLIDER_SHADOW_MASK_U_SIZE: return &(m_options->shadow_mask_u_size);
		case SLIDER_SHADOW_MASK_V_SIZE: return &(m_options->shadow_mask_v_size);
		case SLIDER_SHADOW_MASK_U_OFFSET: return &(m_options->shadow_mask_u_offset);
		case SLIDER_SHADOW_MASK_V_OFFSET: return &(m_options->shadow_mask_v_offset);
		case SLIDER_DISTORTION: return &(m_options->distortion);
		case SLIDER_CUBIC_DISTORTION: return &(m_options->cubic_distortion);
		case SLIDER_DISTORT_CORNER: return &(m_options->distort_corner);
		case SLIDER_ROUND_CORNER: return &(m_options->round_corner);
		case SLIDER_SMOOTH_BORDER: return &(m_options->smooth_border);
		case SLIDER_REFLECTION: return &(m_options->reflection);
		case SLIDER_VIGNETTING: return &(m_options->vignetting);
		case SLIDER_SCANLINE_ALPHA: return &(m_options->scanline_alpha);
		case SLIDER_SCANLINE_SCALE: return &(m_options->scanline_scale);
		case SLIDER_SCANLINE_HEIGHT: return &(m_options->scanline_height);
		case SLIDER_SCANLINE_VARIATION: return &(m_options->scanline_variation);
		case SLIDER_SCANLINE_BRIGHT_SCALE: return &(m_options->scanline_bright_scale);
		case SLIDER_SCANLINE_BRIGHT_OFFSET: return &(m_options->scanline_bright_offset);
		case SLIDER_SCANLINE_JITTER: return &(m_options->scanline_jitter);
		case SLIDER_HUM_BAR_ALPHA: return &(m_options->hum_bar_alpha);
		case SLIDER_DEFOCUS: return &(m_options->defocus[index]);
		case SLIDER_CONVERGE_X: return &(m_options->converge_x[index]);
		case SLIDER_CONVERGE_Y: return &(m_options->converge_y[index]);
		case SLIDER_RADIAL_CONVERGE_X: return &(m_options->radial_converge_x[index]);
		case SLIDER_RADIAL_CONVERGE_Y: return &(m_options->radial_converge_y[index]);
		case SLIDER_RED_RATIO: return &(m_options->red_ratio[index]);
		case SLIDER_GREEN_RATIO: return &(m_options->grn_ratio[index]);
		case SLIDER_BLUE_RATIO: return &(m_options->blu_ratio[index]);
		case SLIDER_SATURATION: return &(m_options->saturation);
		case SLIDER_OFFSET: return &(m_options->offset[index]);
		case SLIDER_SCALE: return &(m_options->scale[index]);
		case SLIDER_POWER: return &(m_options->power[index]);
		case SLIDER_FLOOR: return &(m_options->floor[index]);
		case SLIDER_CHROMA_MODE: return &(m_options->chroma_mode);
		case SLIDER_CHROMA_A: return &(m_options->chroma_a[index]);
		case SLIDER_CHROMA_B: return &(m_options->chroma_b[index]);
		case SLIDER_CHROMA_C: return &(m_options->chroma_c[index]);
		case SLIDER_CHROMA_CONVERSION_GAIN: return &(m_options->chroma_conversion_gain[index]);
		case SLIDER_Y_GAIN: return &(m_options->chroma_y_gain[index]);
		case SLIDER_PHOSPHOR: return &(m_options->phosphor[index]);
		case SLIDER_BLOOM_BLEND_MODE: return &(m_options->bloom_blend_mode);
		case SLIDER_BLOOM_SCALE: return &(m_options->bloom_scale);
		case SLIDER_BLOOM_OVERDRIVE: return &(m_options->bloom_overdrive[index]);
		case SLIDER_BLOOM_LVL0_SCALE: return &(m_options->bloom_level0_weight);
		case SLIDER_BLOOM_LVL1_SCALE: return &(m_options->bloom_level1_weight);
		case SLIDER_BLOOM_LVL2_SCALE: return &(m_options->bloom_level2_weight);
		case SLIDER_BLOOM_LVL3_SCALE: return &(m_options->bloom_level3_weight);
		case SLIDER_BLOOM_LVL4_SCALE: return &(m_options->bloom_level4_weight);
		case SLIDER_BLOOM_LVL5_SCALE: return &(m_options->bloom_level5_weight);
		case SLIDER_BLOOM_LVL6_SCALE: return &(m_options->bloom_level6_weight);
		case SLIDER_BLOOM_LVL7_SCALE: return &(m_options->bloom_level7_weight);
		case SLIDER_BLOOM_LVL8_SCALE: return &(m_options->bloom_level8_weight);
		case SLIDER_BLOOM_SHIFT: return &(m_options->bloom_shift);
		case SLIDER_NTSC_ENABLE: return &(m_options->yiq_enable);
		case SLIDER_NTSC_JITTER: return &(m_options->yiq_jitter);
		case SLIDER_NTSC_A_VALUE: return &(m_options->yiq_a);
		case SLIDER_NTSC_B_VALUE: return &(m_options->yiq_b);
		case SLIDER_NTSC_P_VALUE: return &(m_options->yiq_p);
		case SLIDER_NTSC_O_VALUE: return &(m_options->yiq_o);
		case SLIDER_NTSC_CC_VALUE: return &(m_options->yiq_cc);
		case SLIDER_NTSC_N_VALUE: return &(m_options->yiq_n);
		case SLIDER_NTSC_Y_VALUE: return &(m_options->yiq_y);
		case SLIDER_NTSC_I_VALUE: return &(m_options->yiq_i);
		case SLIDER_NTSC_Q_VALUE: return &(m_options->yiq_q);
		case SLIDER_NTSC_SCAN_TIME: return &(m_options->yiq_scan_time);
		case SLIDER_LUT_ENABLE: return &(m_options->lut_enable);
		case SLIDER_UI_LUT_ENABLE: return &(m_options->ui_lut_enable);
	}
	return nullptr;
}

void d3d11_shaders::init_slider_list()
{
	m_sliders.clear();
	m_core_sliders.clear();

	m_internal_sliders.clear();

	const screen_device *first_screen = screen_device_enumerator(m_machine->root_device()).first();
	if (first_screen == nullptr)
		return;

	int screen_type = first_screen->screen_type();

	for (int i = 0; s_sliders[i].name != nullptr; i++)
	{
		d3d11_slider_desc *desc = &s_sliders[i];
		if ((screen_type == SCREEN_TYPE_VECTOR && (desc->screen_type & SLIDER_SCREEN_TYPE_VECTOR) == SLIDER_SCREEN_TYPE_VECTOR) ||
			(screen_type == SCREEN_TYPE_RASTER && (desc->screen_type & SLIDER_SCREEN_TYPE_RASTER) == SLIDER_SCREEN_TYPE_RASTER) ||
			(screen_type == SCREEN_TYPE_LCD    && (desc->screen_type & SLIDER_SCREEN_TYPE_LCD)    == SLIDER_SCREEN_TYPE_LCD))
		{
			int count;
			switch (desc->slider_type)
			{
				case SLIDER_VEC2:
					count = 2;
					break;
				case SLIDER_COLOR:
					count = 3;
					break;
				default:
					count = 1;
					break;
			}

			for (int j = 0; j < count; j++)
			{
				d3d11_slider &slider_arg = *m_internal_sliders.emplace_back(std::make_unique<d3d11_slider>(desc, get_slider_option(desc->id, j), &m_options->params_dirty));
				std::string name = desc->name;
				switch (desc->slider_type)
				{
					case SLIDER_VEC2:
					{
						char const *const names[2] = { " X", " Y" };
						name += names[j];
						break;
					}
					case SLIDER_COLOR:
					{
						char const *const names[3] = { " Red", " Green", " Blue" };
						name += names[j];
						break;
					}
					default:
						break;
				}

				std::unique_ptr<slider_state> core_slider = slider_alloc(std::move(name), desc->minval, desc->defval, desc->maxval, desc->step, &slider_arg);

				ui::menu_item item(ui::menu_item_type::SLIDER, core_slider.get());
				item.set_text(core_slider->description);
				m_sliders.emplace_back(item);
				m_core_sliders.emplace_back(std::move(core_slider));
			}
		}
	}
}


//============================================================
//  uniform functions
//============================================================

d3d11_uniform::d3d11_uniform(d3d11_effect *shader, D3DXHANDLE param, uniform_type type, int id)
	: m_type(type)
	, m_id(id)
	, m_shader(shader)
	, m_handle(param)
{
}

bool d3d11_uniform::update(d3d11_poly_info *poly)
{
	if (m_id >= CU_COUNT)
		return false;

	d3d11_shaders *shadersys = m_shader->m_shaders;
	d3d11_hlsl_options *options = shadersys->m_options;
	renderer_d3d11 *renderer = shadersys->m_renderer;
	d3d11_texture_info *curr_texture = shadersys->m_curr_texture;

	const screen_device *first_screen = screen_device_enumerator(renderer->window().machine().root_device()).first();
	const bool vector_screen = first_screen != nullptr && first_screen->screen_type() == SCREEN_TYPE_VECTOR;

	switch (m_id)
	{
		case CU_SCREEN_DIMS:
		{
			d3d11_vec2f screendims = renderer->get_dims();
			return m_shader->set_vector("ScreenDims", 2, &screendims.c.x);
		}

		case CU_SCREEN_COUNT:
		{
			int screen_count = renderer->window().target()->current_view().visible_screen_count();
			return m_shader->set_int("ScreenCount", screen_count);
		}

		case CU_SOURCE_DIMS:
		{
			if (vector_screen)
			{
				if (shadersys->m_curr_render_target)
				{
					// vector screen has no source texture, so take the source dimensions of the render target
					float sourcedims[2] = { float(shadersys->m_curr_render_target->width), float(shadersys->m_curr_render_target->height) };
					return m_shader->set_vector("SourceDims", 2, sourcedims);
				}
			}
			else
			{
				if (shadersys->m_curr_texture)
				{
					d3d11_vec2f sourcedims = shadersys->m_curr_texture->get_rawdims();
					return m_shader->set_vector("SourceDims", 2, &sourcedims.c.x);
				}
			}
			break;
		}
		case CU_TARGET_DIMS:
			if (shadersys->m_curr_render_target)
			{
				float targetdims[2] = { float(shadersys->m_curr_render_target->target_width), float(shadersys->m_curr_render_target->target_height) };
				return m_shader->set_vector("TargetDims", 2, targetdims);
			}
			break;

		case CU_TARGET_SCALE:
			if (shadersys->m_curr_render_target)
			{
				float targetscale[2] = { shadersys->m_oversampling_enable ? 2.0f : 1.0f, shadersys->m_oversampling_enable ? 2.0f : 1.0f };
				return m_shader->set_vector("TargetScale", 2, targetscale);
			}
			break;

		case CU_QUAD_DIMS:
			if (shadersys->m_curr_poly)
			{
				float quaddims[2] = { floorf(shadersys->m_curr_poly->prim_width() + 0.5f), floorf(shadersys->m_curr_poly->prim_height() + 0.5f) };
				return m_shader->set_vector("QuadDims", 2, quaddims);
			}
			break;

		case CU_SWAP_XY:
			return m_shader->set_bool("SwapXY", renderer->window().swap_xy());

		case CU_VECTOR_SCREEN:
			return m_shader->set_bool("VectorScreen", vector_screen);
		case CU_VECTOR_TIME_RATIO:
			return m_shader->set_float("TimeRatio", 0.f);
		case CU_VECTOR_TIME_SCALE:
			return m_shader->set_float("TimeScale", 0.f);
		case CU_VECTOR_LENGTH_RATIO:
			return m_shader->set_float("LengthRatio", options->vector_length_ratio);
		case CU_VECTOR_LENGTH_SCALE:
			return m_shader->set_float("LengthScale", options->vector_length_scale);
		case CU_VECTOR_BEAM_SMOOTH:
			return m_shader->set_float("BeamSmooth", options->vector_beam_smooth);

		case CU_BLOOM_LEVEL0_WEIGHT:
			return m_shader->set_float("Level0Weight", options->bloom_level0_weight);
		case CU_BLOOM_LEVEL1_WEIGHT:
			return m_shader->set_float("Level1Weight", options->bloom_level1_weight);
		case CU_BLOOM_LEVEL2_WEIGHT:
			return m_shader->set_float("Level2Weight", options->bloom_level2_weight);
		case CU_BLOOM_LEVEL3_WEIGHT:
			return m_shader->set_float("Level3Weight", options->bloom_level3_weight);
		case CU_BLOOM_LEVEL4_WEIGHT:
			return m_shader->set_float("Level4Weight", options->bloom_level4_weight);
		case CU_BLOOM_LEVEL5_WEIGHT:
			return m_shader->set_float("Level5Weight", options->bloom_level5_weight);
		case CU_BLOOM_LEVEL6_WEIGHT:
			return m_shader->set_float("Level6Weight", options->bloom_level6_weight);
		case CU_BLOOM_LEVEL7_WEIGHT:
			return m_shader->set_float("Level7Weight", options->bloom_level7_weight);
		case CU_BLOOM_LEVEL8_WEIGHT:
			return m_shader->set_float("Level8Weight", options->bloom_level8_weight);
		case CU_BLOOM_BLEND_MODE:
			return m_shader->set_int("BloomBlendMode", options->bloom_blend_mode);
		case CU_BLOOM_SCALE:
			return m_shader->set_float("BloomScale", options->bloom_scale);
		case CU_BLOOM_OVERDRIVE:
			return m_shader->set_vector("BloomOverdrive", 3, options->bloom_overdrive);

		case CU_NTSC_CCFREQ:
			return m_shader->set_float("CCValue", options->yiq_cc);
		case CU_NTSC_A:
			return m_shader->set_float("AValue", options->yiq_a);
		case CU_NTSC_B:
			return m_shader->set_float("BValue", options->yiq_b);
		case CU_NTSC_O:
			return m_shader->set_float("OValue", options->yiq_o);
		case CU_NTSC_P:
			return m_shader->set_float("PValue", options->yiq_p);
		case CU_NTSC_NOTCH:
			return m_shader->set_float("NotchHalfWidth", options->yiq_n);
		case CU_NTSC_YFREQ:
			return m_shader->set_float("YFreqResponse", options->yiq_y);
		case CU_NTSC_IFREQ:
			return m_shader->set_float("IFreqResponse", options->yiq_i);
		case CU_NTSC_QFREQ:
			return m_shader->set_float("QFreqResponse", options->yiq_q);
		case CU_NTSC_HTIME:
			return m_shader->set_float("ScanTime", options->yiq_scan_time);
		case CU_NTSC_ENABLE:
			return m_shader->set_float("YIQEnable", options->yiq_enable ? 1.0f : 0.0f);
		case CU_NTSC_SIGNAL_OFFSET:
			return m_shader->set_float("SignalOffset", curr_texture->get_cur_frame() == 0 ? 0.0f : options->yiq_jitter);

		case CU_COLOR_RED_RATIOS:
			return m_shader->set_vector("RedRatios", 3, options->red_ratio);
		case CU_COLOR_GRN_RATIOS:
			return m_shader->set_vector("GrnRatios", 3, options->grn_ratio);
		case CU_COLOR_BLU_RATIOS:
			return m_shader->set_vector("BluRatios", 3, options->blu_ratio);
		case CU_COLOR_OFFSET:
			return m_shader->set_vector("Offset", 3, options->offset);
		case CU_COLOR_SCALE:
			return m_shader->set_vector("Scale", 3, options->scale);
		case CU_COLOR_SATURATION:
			return m_shader->set_float("Saturation", options->saturation);

		case CU_CONVERGE_LINEAR_X:
			return m_shader->set_vector("ConvergeX", 3, options->converge_x);
		case CU_CONVERGE_LINEAR_Y:
			return m_shader->set_vector("ConvergeY", 3, options->converge_y);
		case CU_CONVERGE_RADIAL_X:
			return m_shader->set_vector("RadialConvergeX", 3, options->radial_converge_x);
		case CU_CONVERGE_RADIAL_Y:
			return m_shader->set_vector("RadialConvergeY", 3, options->radial_converge_y);

		case CU_FOCUS_SIZE:
			return m_shader->set_vector("Defocus", 2, &options->defocus[0]);

		case CU_CHROMA_MODE:
			return m_shader->set_int("ChromaMode", options->chroma_mode);
		case CU_CHROMA_A:
			return m_shader->set_vector("ChromaA", 2, &options->chroma_a[0]);
		case CU_CHROMA_B:
			return m_shader->set_vector("ChromaB", 2, &options->chroma_b[0]);
		case CU_CHROMA_C:
			return m_shader->set_vector("ChromaC", 2, &options->chroma_c[0]);
		case CU_CHROMA_CONVERSION_GAIN:
			return m_shader->set_vector("ConversionGain", 3, &options->chroma_conversion_gain[0]);
		case CU_CHROMA_Y_GAIN:
			return m_shader->set_vector("YGain", 3, &options->chroma_y_gain[0]);

		case CU_PHOSPHOR_LIFE:
			return m_shader->set_vector("Phosphor", 3, options->phosphor);

		case CU_POST_REFLECTION:
			return m_shader->set_float("ReflectionAmount", options->reflection);
		case CU_POST_VIGNETTING:
			return m_shader->set_float("VignettingAmount", options->vignetting);
		case CU_POST_DISTORTION:
			return m_shader->set_float("DistortionAmount", options->distortion);
		case CU_POST_CUBIC_DISTORTION:
			return m_shader->set_float("CubicDistortionAmount", options->cubic_distortion);
		case CU_POST_DISTORT_CORNER:
			return m_shader->set_float("DistortCornerAmount", options->distort_corner);
		case CU_POST_ROUND_CORNER:
			return m_shader->set_float("RoundCornerAmount", options->round_corner);
		case CU_POST_SMOOTH_BORDER:
			return m_shader->set_float("SmoothBorderAmount", options->smooth_border);
		case CU_POST_SHADOW_ALPHA:
			return m_shader->set_float("ShadowAlpha", !shadersys->m_shadow_texture ? 0.0f : options->shadow_mask_alpha);

		case CU_POST_SHADOW_COUNT:
		{
			float shadowcount[2] = { float(options->shadow_mask_count_x), float(options->shadow_mask_count_y) };
			return m_shader->set_vector("ShadowCount", 2, shadowcount);
		}
		case CU_POST_SHADOW_UV:
		{
			float shadowuv[2] = { options->shadow_mask_u_size, options->shadow_mask_v_size };
			return m_shader->set_vector("ShadowUV", 2, shadowuv);
		}
		case CU_POST_SHADOW_UV_OFFSET:
		{
			float shadowuv[2] = { options->shadow_mask_u_offset, options->shadow_mask_v_offset };
			return m_shader->set_vector("ShadowUVOffset", 2, shadowuv);
		}

		case CU_POST_SHADOW_TILE_MODE:
			return m_shader->set_int("ShadowTileMode", options->shadow_mask_tile_mode);

		case CU_POST_SHADOW_DIMS:
		{
			d3d11_vec2f shadow_dims;
			if (shadersys->m_shadow_texture)
			{
				shadow_dims = shadersys->m_shadow_texture->get_rawdims();
			}
			else
			{
				shadow_dims.c.x = 1.0f;
				shadow_dims.c.y = 1.0f;
			}

			return m_shader->set_vector("ShadowDims", 2, &shadow_dims.c.x);
		}
		case CU_POST_SCANLINE_ALPHA:
			return m_shader->set_float("ScanlineAlpha", options->scanline_alpha);
		case CU_POST_SCANLINE_SCALE:
			return m_shader->set_float("ScanlineScale", options->scanline_scale);
		case CU_POST_SCANLINE_HEIGHT:
			return m_shader->set_float("ScanlineHeight", options->scanline_height);
		case CU_POST_SCANLINE_VARIATION:
			return m_shader->set_float("ScanlineVariation", options->scanline_variation);
		case CU_POST_SCANLINE_OFFSET:
			return m_shader->set_float("ScanlineOffset", curr_texture->get_cur_frame() == 0 ? 0.0f : options->scanline_jitter);
		case CU_POST_SCANLINE_BRIGHT_SCALE:
			return m_shader->set_float("ScanlineBrightScale", options->scanline_bright_scale);
		case CU_POST_SCANLINE_BRIGHT_OFFSET:
			return m_shader->set_float("ScanlineBrightOffset", options->scanline_bright_offset);
		case CU_POST_POWER:
			return m_shader->set_vector("Power", 3, options->power);
		case CU_POST_FLOOR:
			return m_shader->set_vector("Floor", 3, options->floor);
		case CU_POST_HUM_BAR_ALPHA:
			return m_shader->set_float("HumBarAlpha", options->hum_bar_alpha);
		case CU_LUT_ENABLE:
			return m_shader->set_bool("LutEnable", options->lut_enable ? true : false);
		case CU_UI_LUT_ENABLE:
			return m_shader->set_bool("UiLutEnable", options->ui_lut_enable ? true : false);

		case CU_PRIM_TINT:
		{
			uint32_t tint = (uint32_t)poly->tint();
			float prim_tint[3] = { ((tint >> 16) & 0xff) / 255.0f, ((tint >> 8) & 0xff) / 255.0f, (tint & 0xff) / 255.0f };
			return m_shader->set_vector("PrimTint", 3, prim_tint);
		}
	}

	return false;
}


//============================================================
//  effect functions
//============================================================

d3d11_effect::d3d11_effect(d3d11_shaders *shadersys, ID3D11Device *d3d11, ID3D11DeviceContext *d3d11_context, const d3d_compile_fn compile_fn, const char *name, const char *path)
	: m_vs(nullptr)
	, m_ps(nullptr)
	, m_d3d11(d3d11)
	, m_d3d11_context(d3d11_context)
	, m_constant_buffer(nullptr)
	, m_shaders(shadersys)
	, m_num_passes(0)
	, m_compile_fn(compile_fn)
	, m_occupied_uniforms(0)
	, m_constants_dirty(true)
	, m_valid(false)
	, m_active(false)
{
	strcpy(m_name, name);

	char name_cstr[1024];
	sprintf(name_cstr, "%s\\%s", path, name);
	auto effect_name = osd::text::to_tstring(name_cstr);

	ID3DBlob *vs_results = nullptr;
	ID3DBlob *vs_blob = nullptr;
	HRESULT result = (m_compile_fn)(effect_name.c_str(), nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vs_blob, &vs_results);
	if (FAILED(result))
	{
		osd_printf_error("Direct3D11: Compilation of vertex shader %s failed: %08x\n", name_cstr, (uint32_t)result);
		const char *results = (const char *)vs_results->GetBufferPointer();
		size_t results_size = (size_t)vs_results->GetBufferSize();
		for (size_t i = 0; i < results_size; i++)
		{
			osd_printf_error("%c", results[i]);
		}
		osd_printf_error("\n");
		return;
	}
	d3d11->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &m_vs);

	ID3DBlob *ps_results = nullptr;
	ID3DBlob *ps_blob = nullptr;
	result = (m_compile_fn)(effect_name.c_str(), nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &ps_blob, &ps_results);
	if (FAILED(result))
	{
		osd_printf_error("Direct3D11: Compilation of pixel shader %s failed: %08x\n", name_cstr, (uint32_t)result);
		const char *results = (const char *)ps_results->GetBufferPointer();
		size_t results_size = (size_t)ps_results->GetBufferSize();
		for (size_t i = 0; i < results_size; i++)
		{
			osd_printf_error("%c", results[i]);
		}
		osd_printf_error("\n");
		return;
	}
	d3d11->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &m_ps);

	m_valid = true;
}

d3d11_effect::~d3d11_effect()
{
	if (m_ps != nullptr)
	{
		m_ps->Release();
		m_ps = nullptr;
	}

	if (m_vs != nullptr)
	{
		m_vs->Release();
		m_vs = nullptr;
	}

	if (m_constant_buffer != nullptr)
	{
		m_constant_buffer->Release();
		m_constant_buffer = nullptr;
	}

	m_uniform_offsets.clear();
}

void d3d11_effect::add_uniform(D3DXHANDLE param, d3d11_uniform::uniform_type type, int id)
{
	uint32_t uniform_size = 1;
	switch (type)
	{
		case d3d11_uniform::UT_VEC4:
			uniform_size = 4;
			break;
		case d3d11_uniform::UT_VEC3:
			uniform_size = 3;
			break;
		case d3d11_uniform::UT_VEC2:
			uniform_size = 2;
			break;
		default:
			uniform_size = 1;
			break;
	}

	const uint32_t buffer_line_words_free = 4 - (m_occupied_uniforms & 3);
	if (buffer_line_words_free < uniform_size)
		m_occupied_uniforms += buffer_line_words_free;
	m_uniform_offsets[param] = m_occupied_uniforms;
	m_occupied_uniforms += uniform_size;
	m_uniform_list.push_back(std::make_unique<d3d11_uniform>(this, param, type, id));
}

void d3d11_effect::update_uniforms(d3d11_poly_info *poly, ID3D11RenderTargetView *dst, ID3D11DepthStencilView *dst_depth)
{
	m_d3d11_context->OMSetRenderTargets(1, &dst, dst_depth);

	for (auto &uniform : m_uniform_list)
		(*uniform).update(poly);

	D3D11_MAPPED_SUBRESOURCE mapped_constants;
	m_d3d11_context->Map(m_constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_constants);
	float* constants = (float *)mapped_constants.pData;
	memcpy(constants, m_uniform_data, sizeof(float) * m_occupied_uniforms);
	m_d3d11_context->Unmap(m_constant_buffer, 0);

	m_d3d11_context->VSSetShader(m_vs, nullptr, 0);
	m_d3d11_context->VSSetConstantBuffers(0, 1, &m_constant_buffer);
	m_d3d11_context->PSSetShader(m_ps, nullptr, 0);
	m_d3d11_context->PSSetConstantBuffers(0, 1, &m_constant_buffer);
}

void d3d11_effect::finalize_uniforms()
{
	D3D11_BUFFER_DESC constant_buffer_desc = { 0 };
	constant_buffer_desc.ByteWidth      = ((sizeof(uint32_t) * m_occupied_uniforms) + 0xf) & 0xfffffff0; // round constant buffer size to a 16-byte boundary
	constant_buffer_desc.Usage          = D3D11_USAGE_DYNAMIC;
	constant_buffer_desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
	constant_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	m_d3d11->CreateBuffer(&constant_buffer_desc, nullptr, &m_constant_buffer);
}

void d3d11_effect::begin()
{
	m_active = true;
	m_num_passes = 0;
}

void d3d11_effect::end()
{
	ID3D11ShaderResourceView *const blank_srv[1] = { nullptr };
	for (uint32_t i = 0; i < 16; i++)
	{
		m_d3d11_context->PSSetShaderResources(i, 1, blank_srv);
	}
	m_active = false;
}

bool d3d11_effect::set_vector(D3DXHANDLE param, uint32_t count, float *vector)
{
	count = std::min(count, 4U);

	auto iter = m_uniform_offsets.find(param);
	if (iter != m_uniform_offsets.end())
	{
		uint32_t data_offset = iter->second;
		if (!memcmp(&m_uniform_data[data_offset], vector, sizeof(float) * count))
			return false;
		memcpy(&m_uniform_data[data_offset], vector, sizeof(float) * count);
	}
	else
	{
		const uint32_t buffer_line_words_free = 4 - (m_occupied_uniforms & 3);
		if (buffer_line_words_free < count)
			m_occupied_uniforms += buffer_line_words_free;
		m_uniform_offsets[param] = m_occupied_uniforms;
		memcpy(&m_uniform_data[m_occupied_uniforms], vector, sizeof(float) * count);
		m_occupied_uniforms += count;
	}
	m_constants_dirty = true;
	return true;
}

bool d3d11_effect::set_float(D3DXHANDLE param, float value)
{
	auto iter = m_uniform_offsets.find(param);
	if (iter != m_uniform_offsets.end())
	{
		uint32_t data_offset = iter->second;
		if (m_uniform_data[data_offset] == value)
			return false;
		m_uniform_data[data_offset] = value;
	}
	else
	{
		m_uniform_offsets[param] = m_occupied_uniforms;
		m_uniform_data[m_occupied_uniforms] = value;
		m_occupied_uniforms++;
	}
	m_constants_dirty = true;
	return true;
}

bool d3d11_effect::set_int(D3DXHANDLE param, int value)
{
	auto iter = m_uniform_offsets.find(param);
	if (iter != m_uniform_offsets.end())
	{
		uint32_t data_offset = iter->second;
		if (m_uniform_data[data_offset] == float(value))
			return false;
		m_uniform_data[data_offset] = float(value);
	}
	else
	{
		m_uniform_offsets[param] = m_occupied_uniforms;
		m_uniform_data[m_occupied_uniforms] = float(value);
		m_occupied_uniforms++;
	}
	m_constants_dirty = true;
	return true;
}

bool d3d11_effect::set_bool(D3DXHANDLE param, bool value)
{
	auto iter = m_uniform_offsets.find(param);
	if (iter != m_uniform_offsets.end())
	{
		uint32_t data_offset = iter->second;
		if (m_uniform_data[data_offset] == float(value))
			return false;
		m_uniform_data[data_offset] = float(value);
	}
	else
	{
		m_uniform_offsets[param] = m_occupied_uniforms;
		m_uniform_data[m_occupied_uniforms] = float(value);
		m_occupied_uniforms++;
	}
	m_constants_dirty = true;
	return true;
}

void d3d11_effect::set_texture(uint32_t slot, ID3D11ShaderResourceView * const *tex)
{
	m_d3d11_context->PSSetShaderResources(slot, 1, tex);
}
