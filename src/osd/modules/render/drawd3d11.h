// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  drawd3d11.h - Win32 Direct3D11 header
//
//============================================================
#ifndef MAME_OSD_MODULES_RENDER_DRAWD3D11_H
#define MAME_OSD_MODULES_RENDER_DRAWD3D11_H

#pragma once

#include "d3d/d3d11comm.h"

#include "modules/lib/osdlib.h"
#include "modules/osdwindow.h"
#include "render_module.h"

// from OSD implementation
#include "window.h"
#include "winmain.h"

// general OSD headers
#include "modules/monitor/monitor_module.h"

#include "sliderdirtynotifier.h"

#include <windows.h>
#include <tchar.h>
#include <mmsystem.h>
#include <d3dx9.h>
#include <d3d11.h>
#undef interface

#include <memory>
#include <vector>
#include <cmath>


//============================================================
//  CONSTANTS
//============================================================

#define VERTEX_BUFFER_LENGTH  (61440)

//============================================================
//  TYPE DEFINITIONS
//============================================================

class d3d11_shaders;
struct hlsl_options;

/* renderer_d3d11 is the information about D3D11 for the current screen */
class renderer_d3d11 : public osd_renderer, public slider_dirty_notifier
{
public:
	using d3d11_create_fn = HRESULT (WINAPI *)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
	using d3d_compile_fn = HRESULT (WINAPI *)(LPCWSTR, CONST D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

	renderer_d3d11(osd_window &window, const d3d11_create_fn create_fn, const d3d_compile_fn compile_fn);
	virtual ~renderer_d3d11();

	virtual bool 				create() override;
	virtual render_primitive_list *get_primitives() override;
	virtual bool 				draw(const int update) override;
	virtual void 				save() override;
	virtual void 				record() override;
	virtual void 				toggle_fsfx() override;
	virtual void 				add_audio_to_recording(const int16_t *buffer, int samples_this_frame) override;
	virtual std::vector<ui::menu_item> get_slider_list() override;
	virtual void 				set_sliders_dirty() override;

	bool                    	initialize();

	bool                    	device_create();
	bool                    	device_create_resources();
	void                    	device_create_blend_states();
	void                    	device_create_sampler_states();
	void                    	device_delete_resources();
	void                    	update_presentation_parameters();
	void                    	update_gamma_ramp();

	bool                    	device_verify_gamma_caps();
	bool                    	device_test_cooperative();

	bool                    	config_adapter_mode();
	void                    	pick_best_mode(uint32_t mode_count, std::unique_ptr<DXGI_MODE_DESC []> &modes);
	bool                    	get_adapter_for_monitor(DXGI_ADAPTER_DESC *adapter_desc);

	bool                    	update_window_size();

	bool                    	pre_window_draw_check();
	void                    	begin_frame();
	void                    	end_frame();

	void                    	draw_line(const render_primitive &prim);
	void                    	draw_quad(const render_primitive &prim);
	void                    	batch_vector(const render_primitive &prim);
	void                    	batch_vectors(int vector_count);

	d3d11_vertex *          	mesh_alloc(int numverts);

	void                    	process_primitives();
	void                    	primitive_flush_pending();

	void                    	set_texture(d3d11_texture_info *texture);
	void                    	set_sampler_mode(const uint32_t slot, const bool linear_filter, const D3D11_TEXTURE_ADDRESS_MODE mode, const bool force_set = false);
	void                    	set_blendmode(const int blendmode, const bool force_set = false);
	void                    	reset_render_states();

	// Setters / getters
	int                     	get_width() const { return m_width; }
	d3d11_vec2f             	get_dims() const { return d3d11_vec2f(m_width, m_height); }
	int                     	get_height() const { return m_height; }
	uint32_t                	get_refresh_numerator() const { return m_refresh_num; }
	uint32_t                	get_refresh_denominator() const { return m_refresh_denom; }
	bool                    	post_fx_available() const { return m_post_fx_available; }
	void                    	set_post_fx_unavailable() { m_post_fx_available = false; }

	ID3D11Device *          	get_device() const { return m_d3d11; }
	ID3D11DeviceContext *   	get_context() const { return m_d3d11_context; }
	ID3D11DepthStencilView *	get_depthbuffer() const { return m_depthbuffer_view; }
	ID3D11RenderTargetView *	get_framebuffer() const { return m_framebuffer_view; }
	ID3D11RenderTargetView * const * get_framebuffer_ptr() const { return &m_framebuffer_view; }
	DXGI_SWAP_CHAIN_DESC *  	get_presentation() { return &m_presentation; }
	D3D11_VIEWPORT          	get_viewport() const { return m_viewport; }

	ID3D11Buffer *          	get_vertex_buffer() const { return m_vertexbuf; }
	ID3D11Buffer *          	get_index_buffer() const { return m_indexbuf; }

	void                    	set_toggle(bool toggle) { m_toggle = toggle; }

	DXGI_FORMAT             	get_pixel_format() const { return m_pixformat; }
	DXGI_MODE_DESC          	get_origmode() const { return m_origmode; }

	d3d11_texture_manager * 	get_texture_manager() const { return m_texture_manager.get(); }
	d3d11_texture_info *    	get_default_texture();

	d3d11_shaders *         	get_shaders() const { return m_shaders.get(); }

private:
	bool					    load_shader(const char *filename, ID3D11VertexShader **vs, ID3D11PixelShader **ps,
									D3D11_INPUT_ELEMENT_DESC *layout_desc, ID3D11InputLayout **layout);

	void						init_blit_quad();

	d3d11_create_fn             m_create_fn;
	d3d_compile_fn              m_compile_fn;
	ID3D11Device               *m_d3d11;                    // Direct3D 11 API object
	ID3D11DeviceContext        *m_d3d11_context;            // Direct3D 11 Context API object
	IDXGIFactory               *m_factory;
	IDXGIAdapter               *m_adapter;
	int                         m_adapter_num;              // ordinal adapter number
	IDXGIOutput                *m_output;
	DXGI_OUTPUT_DESC            m_output_desc;
	int                         m_output_num;               // ordinal output number
	int                         m_width;                    // current width
	int                         m_height;                   // current height
	uint32_t                    m_refresh_num;              // current refresh rate, numerator
	uint32_t                    m_refresh_denom;            // current refresh rate, denominator
	bool                        m_post_fx_available;
	IDXGISwapChain             *m_swap_chain;
	int                         m_sync_interval;

	uint32_t                    m_gamma_points;             // how many gamma control points are supported?
	float                       m_gamma_min;                // minimum converted gamma value
	float                       m_gamma_max;                // maximum converted gamma value
	DXGI_SWAP_CHAIN_DESC        m_presentation;             // set of presentation parameters for our swap chain
	DXGI_MODE_DESC              m_origmode;                 // original display mode for our adapter and output
	DXGI_FORMAT                 m_pixformat;                // pixel format we are using

	d3d11_vertex *              m_vectorbatch;              // pointer to the vector batch buffer
	int                         m_batchindex;               // current index into the vector batch

	d3d11_poly_info             m_poly[VERTEX_BUFFER_LENGTH/3];// array to hold polygons as they are created
	int                         m_numpolys;                 // number of accumulated polygons

	bool                        m_toggle;                   // if shader effects are enabled

	d3d11_texture_info *        m_last_texture;             // previous texture
	int                         m_blendmode;                // current blendmode
	bool                        m_linear_filter[16];        // current texture filter mode per sampler slot
	D3D11_TEXTURE_ADDRESS_MODE  m_sampler_mode[16];      // current texture wrapping mode per sampler slot
	bool                        m_force_render_states;      // whether to force state-setting (used at the beginning of a frame)

	bool                        m_device_initialized;       // whether we have called device_create() yet, or need to again

	ID3D11BlendState           *m_blend_states[BLENDMODE_COUNT];
	ID3D11SamplerState         *m_sampler_states[2][3];

	ID3D11Texture2D            *m_framebuffer;
	ID3D11RenderTargetView     *m_framebuffer_view;
	ID3D11Texture2D            *m_depthbuffer;
	ID3D11DepthStencilView     *m_depthbuffer_view;

	ID3D11VertexShader	       *m_vs;
	ID3D11VertexShader 	       *m_vs_bcg;
	ID3D11VertexShader 	       *m_vs_palette16;
	ID3D11VertexShader 	       *m_vs_yuy16;

	ID3D11PixelShader  	       *m_ps;
	ID3D11PixelShader  	       *m_ps_bcg;
	ID3D11PixelShader  	       *m_ps_palette16;
	ID3D11PixelShader  	       *m_ps_yuy16;

	ID3D11RasterizerState  	   *m_rasterizer_state;
	ID3D11DepthStencilState    *m_depth_stencil_state;

	struct pipeline_constants
	{
		float screen_dims[2];
		float target_dims[2];
		float source_dims[2];
	};
	ID3D11Buffer *m_constant_buffer;

	ID3D11InputLayout *m_input_layout;
	ID3D11InputLayout *m_input_layout_bcg;
	ID3D11InputLayout *m_input_layout_palette16;

	std::unique_ptr<d3d11_shaders> m_shaders; // Shader interface

	d3d11_vertex    m_vertex_data[VERTEX_BUFFER_LENGTH];
	ID3D11Buffer   *m_vertexbuf;
	D3D11_MAPPED_SUBRESOURCE m_lockedbuf;
	int             m_numverts;

	uint32_t        m_index_data[(VERTEX_BUFFER_LENGTH / 4) * 6]; // 6 indices for every 4 vertices
	ID3D11Buffer   *m_indexbuf;

	D3D11_VIEWPORT  m_viewport;

	std::unique_ptr<d3d11_texture_manager> m_texture_manager;          // texture manager
};

#endif // MAME_OSD_MODULES_RENDER_DRAWD3D11_H
