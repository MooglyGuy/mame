// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
//============================================================
//
//  d3d11hlsl.cpp - Direct3D 11 HLSL-specific header
//
//============================================================

#ifndef __WIN_D3D11HLSL__
#define __WIN_D3D11HLSL__

#include "../frontend/mame/ui/menuitem.h"
#include "../frontend/mame/ui/slider.h"
#include "modules/lib/osdlib.h"

#include <wrl/client.h>

#include <map>
#include <memory>
#include <vector>

//============================================================
//  TYPE DEFINITIONS
//============================================================

struct slider_state;

class d3d11_effect;
class d3d11_shaders;

class d3d11_uniform
{
public:
	typedef enum : uint32_t
	{
		UT_VEC4,
		UT_VEC3,
		UT_VEC2,
		UT_FLOAT,
		UT_INT,
		UT_BOOL
	} uniform_type;

	enum : uint32_t
	{
		CU_SCREEN_DIMS = 0,
		CU_SCREEN_COUNT,
		CU_SOURCE_DIMS,
		CU_TARGET_DIMS,
		CU_TARGET_SCALE,
		CU_QUAD_DIMS,

		CU_SWAP_XY,
		CU_VECTOR_SCREEN,

		CU_NTSC_CCFREQ,
		CU_NTSC_A,
		CU_NTSC_B,
		CU_NTSC_O,
		CU_NTSC_P,
		CU_NTSC_NOTCH,
		CU_NTSC_YFREQ,
		CU_NTSC_IFREQ,
		CU_NTSC_QFREQ,
		CU_NTSC_HTIME,
		CU_NTSC_ENABLE,
		CU_NTSC_SIGNAL_OFFSET,

		CU_BLOOM_LEVEL0_WEIGHT,
		CU_BLOOM_LEVEL1_WEIGHT,
		CU_BLOOM_LEVEL2_WEIGHT,
		CU_BLOOM_LEVEL3_WEIGHT,
		CU_BLOOM_LEVEL4_WEIGHT,
		CU_BLOOM_LEVEL5_WEIGHT,
		CU_BLOOM_LEVEL6_WEIGHT,
		CU_BLOOM_LEVEL7_WEIGHT,
		CU_BLOOM_LEVEL8_WEIGHT,
		CU_BLOOM_BLEND_MODE,
		CU_BLOOM_SCALE,
		CU_BLOOM_OVERDRIVE,

		CU_COLOR_RED_RATIOS,
		CU_COLOR_GRN_RATIOS,
		CU_COLOR_BLU_RATIOS,
		CU_COLOR_OFFSET,
		CU_COLOR_SCALE,
		CU_COLOR_SATURATION,

		CU_CONVERGE_LINEAR_X,
		CU_CONVERGE_LINEAR_Y,
		CU_CONVERGE_RADIAL_X,
		CU_CONVERGE_RADIAL_Y,

		CU_FOCUS_SIZE,

		CU_PHOSPHOR_LIFE,

		CU_POST_VIGNETTING,
		CU_POST_DISTORTION,
		CU_POST_CUBIC_DISTORTION,
		CU_POST_DISTORT_CORNER,
		CU_POST_ROUND_CORNER,
		CU_POST_SMOOTH_BORDER,
		CU_POST_REFLECTION,
		CU_POST_SHADOW_ALPHA,
		CU_POST_SHADOW_COUNT,
		CU_POST_SHADOW_UV,
		CU_POST_SHADOW_UV_OFFSET,
		CU_POST_SHADOW_DIMS,
		CU_POST_SHADOW_TILE_MODE,
		CU_POST_SCANLINE_ALPHA,
		CU_POST_SCANLINE_SCALE,
		CU_POST_SCANLINE_HEIGHT,
		CU_POST_SCANLINE_VARIATION,
		CU_POST_SCANLINE_OFFSET,
		CU_POST_SCANLINE_BRIGHT_SCALE,
		CU_POST_SCANLINE_BRIGHT_OFFSET,
		CU_POST_POWER,
		CU_POST_FLOOR,
		CU_POST_HUM_BAR_ALPHA,
		CU_CHROMA_MODE,
		CU_CHROMA_A,
		CU_CHROMA_B,
		CU_CHROMA_C,
		CU_CHROMA_CONVERSION_GAIN,
		CU_CHROMA_Y_GAIN,
		CU_LUT_ENABLE,
		CU_UI_LUT_ENABLE,
		CU_PRIM_TINT,

		CU_COUNT
	};

	d3d11_uniform(d3d11_effect *shader, const char *name, uniform_type type, int id);

	bool        update(d3d11_poly_info *poly);

protected:
	uniform_type  m_type;
	int           m_id;
	d3d11_effect *m_shader;
	D3DXHANDLE    m_handle;
};

class d3d11_effect
{
	friend class d3d11_uniform;

public:
	using d3d_compile_fn = HRESULT (WINAPI *)(LPCWSTR, CONST D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

	d3d11_effect(d3d11_shaders *shadersys, ID3D11Device *d3d11, ID3D11DeviceContext *d3d11_context, const d3d_compile_fn compile_fn, const char *name, const char *path);
	~d3d11_effect();

	void        begin();
	void        end();

	bool        set_vector(D3DXHANDLE param, int count, float *vector);
	bool        set_float(D3DXHANDLE param, float value);
	bool        set_int(D3DXHANDLE param, int value);
	bool        set_bool(D3DXHANDLE param, bool value);
	void        set_texture(uint32_t slot, ID3D11ShaderResourceView * const *tex);

	void        add_uniform(D3DXHANDLE param, d3d11_uniform::uniform_type type, int id);
	void        update_uniforms(d3d11_poly_info *poly);
	void        finalize_uniforms();

	d3d11_shaders *get_shaders() { return m_shaders; }
	uint32_t       num_passes() { return m_num_passes; }

	bool        is_valid() { return m_valid; }
	bool        is_active() { return m_active; }

private:
	std::vector<std::unique_ptr<d3d11_uniform>> m_uniform_list;

	ID3D11VertexShader * m_vs;
	ID3D11PixelShader *  m_ps;
	ID3D11Device *       m_d3d11;
	ID3D11DeviceContext *m_d3d11_context;
	ID3D11Buffer *       m_constant_buffer;
	d3d11_shaders *      m_shaders;
	uint32_t             m_num_passes;
	d3d_compile_fn       m_compile_fn;
	float                m_uniform_data[256];
	uint32_t             m_occupied_uniforms;
	bool                 m_constants_dirty;

	std::map<D3DXHANDLE, D3DXVECTOR4> m_vecs;
	std::map<D3DXHANDLE, float> m_floats;
	std::map<D3DXHANDLE, int> m_ints;
	std::map<D3DXHANDLE, bool> m_bools;
	std::map<D3DXHANDLE, uint32_t> m_uniform_offsets;

	bool        m_valid;
	bool        m_active;
};

class d3d11_render_target;
class renderer_d3d11;
class d3d11_movie_recorder;

/* d3d11_hlsl_options is the information about runtime-mutable Direct3D11 HLSL options */
/* in the future this will be moved into an OSD/emu shared buffer */
struct d3d11_hlsl_options
{
	bool                    params_init = false;
	bool                    params_dirty = false;
	int                     shadow_mask_tile_mode = 0;
	float                   shadow_mask_alpha = 0.0;
	char                    shadow_mask_texture[1024]{ 0 };
	int                     shadow_mask_count_x = 0;
	int                     shadow_mask_count_y = 0;
	float                   shadow_mask_u_size = 0.0;
	float                   shadow_mask_v_size = 0.0;
	float                   shadow_mask_u_offset = 0.0;
	float                   shadow_mask_v_offset = 0.0;
	float                   distortion = 0.0;
	float                   cubic_distortion = 0.0;
	float                   distort_corner = 0.0;
	float                   round_corner = 0.0;
	float                   smooth_border = 0.0;
	float                   reflection = 0.0;
	float                   vignetting = 0.0;
	float                   scanline_alpha = 0.0;
	float                   scanline_scale = 0.0;
	float                   scanline_height = 0.0;
	float                   scanline_variation = 0.0;
	float                   scanline_bright_scale = 0.0;
	float                   scanline_bright_offset = 0.0;
	float                   scanline_jitter = 0.0;
	float                   hum_bar_alpha = 0.0;
	float                   defocus[2]{ 0.0 };
	float                   converge_x[3]{ 0.0 };
	float                   converge_y[3]{ 0.0 };
	float                   radial_converge_x[3]{ 0.0 };
	float                   radial_converge_y[3]{ 0.0 };
	float                   red_ratio[3]{ 0.0 };
	float                   grn_ratio[3]{ 0.0 };
	float                   blu_ratio[3]{ 0.0 };
	float                   offset[3]{ 0.0 };
	float                   scale[3]{ 0.0 };
	float                   power[3]{ 0.0 };
	float                   floor[3]{ 0.0 };
	float                   phosphor[3]{ 0.0 };
	float                   saturation = 0.0;
	int                     chroma_mode = 0;
	float                   chroma_a[2]{ 0.0 };
	float                   chroma_b[2]{ 0.0 };
	float                   chroma_c[2]{ 0.0 };
	float                   chroma_conversion_gain[3]{ 0.0 };
	float                   chroma_y_gain[3]{ 0.0 };

	// NTSC
	int                     yiq_enable = 0;
	float                   yiq_jitter = 0.0;
	float                   yiq_cc = 0.0;
	float                   yiq_a = 0.0;
	float                   yiq_b = 0.0;
	float                   yiq_o = 0.0;
	float                   yiq_p = 0.0;
	float                   yiq_n = 0.0;
	float                   yiq_y = 0.0;
	float                   yiq_i = 0.0;
	float                   yiq_q = 0.0;
	float                   yiq_scan_time = 0.0;
	int                     yiq_phase_count = 0;

	// Vectors
	float                   vector_beam_smooth = 0.0;
	float                   vector_length_scale = 0.0;
	float                   vector_length_ratio = 0.0;

	// Bloom
	int                     bloom_blend_mode = 0;
	float                   bloom_scale = 0.0;
	float                   bloom_overdrive[3]{ 0.0 };
	float                   bloom_level0_weight = 0.0;
	float                   bloom_level1_weight = 0.0;
	float                   bloom_level2_weight = 0.0;
	float                   bloom_level3_weight = 0.0;
	float                   bloom_level4_weight = 0.0;
	float                   bloom_level5_weight = 0.0;
	float                   bloom_level6_weight = 0.0;
	float                   bloom_level7_weight = 0.0;
	float                   bloom_level8_weight = 0.0;

	// Final
	char lut_texture[1024]{ 0 };
	int lut_enable = 0;
	char ui_lut_texture[1024]{ 0 };
	int ui_lut_enable = 0;
};

struct d3d11_slider_desc
{
	const char *                name;
	int                         minval;
	int                         defval;
	int                         maxval;
	int                         step;
	int                         slider_type;
	int                         screen_type;
	int                         id;
	float                       scale;
	const char *                format;
	std::vector<const char *>   strings;
};

class d3d11_slider
{
public:
	d3d11_slider(d3d11_slider_desc *desc, void *value, bool *dirty) : m_desc(desc), m_value(value) { }

	int32_t update(std::string *str, int32_t newval);

private:
	d3d11_slider_desc *m_desc;
	void              *m_value;
};

class d3d11_shaders
{
	friend class d3d11_effect;
	friend class d3d11_uniform;

public:
	using d3d_compile_fn = HRESULT (WINAPI *)(LPCWSTR, CONST D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

	// construction/destruction
	d3d11_shaders();
	~d3d11_shaders();

	bool init(ID3D11Device *d3d11, ID3D11DeviceContext *d3d11_context, const d3d_compile_fn compile_fn, running_machine *machine, renderer_d3d11 *renderer);

	bool enabled() { return m_post_fx_enable && m_renderer->post_fx_available(); }
	void toggle() { m_post_fx_enable = m_initialized && !m_post_fx_enable; }

	void begin_frame(render_primitive_list *primlist);
	void end_frame();

	void begin_draw();
	void end_draw();

	void render_quad(d3d11_poly_info *poly, int vertnum);

	bool create_vector_target(render_primitive *prim, int screen);
	d3d11_render_target* get_vector_target(render_primitive *prim, int screen);
	bool create_texture_target(render_primitive *prim, int width, int height, int screen);
	d3d11_render_target* get_texture_target(render_primitive *prim, int width, int height, int screen);
	bool add_render_target(render_primitive *prim, int source_width, int source_height, int source_screen, int target_width, int target_height);

	void save_snapshot();
	void record_movie();
	void record_audio(const int16_t *buffer, int samples_this_frame);

	void init_fsfx_quad();

	void set_texture(d3d11_texture_info *info);
	void set_filter(bool filter_screens);
	void remove_render_target(int source_width, int source_height, uint32_t screen_index);
	void remove_render_target(d3d11_render_target *rt);

	bool create_resources();
	void delete_resources();

	// slider-related functions
	std::unique_ptr<slider_state> slider_alloc(std::string &&title, int32_t minval, int32_t defval, int32_t maxval, int32_t incval, d3d11_slider *arg);
	void init_slider_list();
	std::vector<ui::menu_item> get_slider_list() { return m_sliders; }
	void *get_slider_option(int id, int index = 0);

private:
	void                    set_curr_effect(d3d11_effect *curr_effect);
	//m_
	void                    blit(ID3D11RenderTargetView *dst, bool clear_dst);

	void                    render_snapshot(ID3D11Texture2D *texture);
	// Time since last call, only updates once per render of all screens
	double                  delta_time() { return m_delta_t; }
	d3d11_render_target*    find_render_target(int source_width, int source_height, uint32_t screen_index);

	rgb_t                   apply_color_convolution(rgb_t color);

	// Shader passes
	void                    ntsc_pass(d3d11_render_target *rt, d3d11_poly_info *poly);
	void                    color_convolution_pass(d3d11_render_target *rt, d3d11_poly_info *poly);
	void                    prescale_pass(d3d11_render_target *rt, d3d11_poly_info *poly);
	void                    deconverge_pass(d3d11_render_target *rt, d3d11_poly_info *poly);
	void                    scanline_pass(d3d11_render_target *rt, d3d11_poly_info *poly);
	void                    defocus_pass(d3d11_render_target *rt, d3d11_poly_info *poly);
	void                    phosphor_pass(d3d11_render_target *rt, d3d11_poly_info *poly);
	void                    post_pass(d3d11_render_target *rt, d3d11_poly_info *poly, bool prepare_bloom);
	void                    downsample_pass(d3d11_render_target *rt, d3d11_poly_info *poly);
	void                    bloom_pass(d3d11_render_target *rt, d3d11_poly_info *poly);
	void                    chroma_pass(d3d11_render_target *rt, d3d11_poly_info *poly);
	void                    distortion_pass(d3d11_render_target *rt, d3d11_poly_info *poly);
	void                    vector_pass(d3d11_render_target *rt, d3d11_poly_info *poly);
	void                    vector_buffer_pass(d3d11_render_target *rt, d3d11_poly_info *poly);
	void                    screen_pass(d3d11_render_target *rt, d3d11_poly_info *poly);
	void                    ui_pass(d3d11_poly_info *poly, int vertnum);

	d3d_compile_fn                m_compile_fn;
	ID3D11Device *                m_d3d11;                  // Direct3D 11 API object
	ID3D11DeviceContext *         m_d3d11_context;          // Direct3D 11 Context API object

	running_machine *             m_machine;
	renderer_d3d11 *              m_renderer;               // D3D11 renderer

	bool                          m_post_fx_enable;         // overall enable flag
	bool                          m_oversampling_enable;    // oversampling enable flag
	int                           m_num_screens;            // number of emulated physical screens
	int                           m_num_targets;            // number of emulated screen targets (can be different from above; cf. artwork and Laserdisc games)
	int                           m_curr_target;            // current target index for render target operations
	int                           m_targets_per_screen[256];// screen target count per screen device/container index; estimated maximum count for array size
	int                           m_target_to_screen[256];  // lookup from target index to screen device/container index; estimated maximum count for array size
	double                        m_acc_t;                  // accumulated machine time
	double                        m_delta_t;                // data for delta_time
	bitmap_argb32                 m_shadow_bitmap;          // shadow mask bitmap for post-processing shader
	d3d11_texture_info *          m_shadow_texture;         // shadow mask texture for post-processing shader
	bitmap_argb32                 m_lut_bitmap;
	d3d11_texture_info *          m_lut_texture;
	bitmap_argb32                 m_ui_lut_bitmap;
	d3d11_texture_info *          m_ui_lut_texture;
	d3d11_hlsl_options *          m_options;                // current options

	ID3D11Texture2D *             m_black_texture;          // black dummy texture
	ID3D11ShaderResourceView *    m_black_view;             // black dummy texture view

	bool                          m_recording_movie;        // ongoing movie recording
	std::unique_ptr<d3d11_movie_recorder> m_recorder;       // HLSL post-render movie recorder

	bool                          m_render_snap;            // whether or not to take HLSL post-render snapshot
	ID3D11Texture2D *             m_snap_copy_texture;      // snapshot destination texture in system memory
	ID3D11Texture2D *             m_snap_texture;           // snapshot upscaled texture
	ID3D11ShaderResourceView *    m_snap_view;              // snapshot upscaled view
	ID3D11RenderTargetView *      m_snap_target;            // snapshot render target
	int                           m_snap_width;             // snapshot width
	int                           m_snap_height;            // snapshot height

	bool                          m_initialized;            // whether or not we're initialized

	// HLSL effects
	//IDirect3DSurface9Ptr          m_backbuffer;             // pointer to our device's backbuffer
	d3d11_effect *                m_curr_effect;            // pointer to the currently active effect object
	std::unique_ptr<d3d11_effect> m_default_effect;         // pointer to the primary-effect object
	std::unique_ptr<d3d11_effect> m_ui_effect;              // pointer to the UI-element effect object
	std::unique_ptr<d3d11_effect> m_vector_buffer_effect;   // pointer to the vector-buffering effect object
	std::unique_ptr<d3d11_effect> m_prescale_effect;        // pointer to the prescale-effect object
	std::unique_ptr<d3d11_effect> m_post_effect;            // pointer to the post-effect object
	std::unique_ptr<d3d11_effect> m_distortion_effect;      // pointer to the distortion-effect object
	std::unique_ptr<d3d11_effect> m_scanline_effect;
	std::unique_ptr<d3d11_effect> m_focus_effect;           // pointer to the focus-effect object
	std::unique_ptr<d3d11_effect> m_phosphor_effect;        // pointer to the phosphor-effect object
	std::unique_ptr<d3d11_effect> m_deconverge_effect;      // pointer to the deconvergence-effect object
	std::unique_ptr<d3d11_effect> m_color_effect;           // pointer to the color-effect object
	std::unique_ptr<d3d11_effect> m_ntsc_effect;            // pointer to the NTSC effect object
	std::unique_ptr<d3d11_effect> m_bloom_effect;           // pointer to the bloom composite effect
	std::unique_ptr<d3d11_effect> m_downsample_effect;      // pointer to the bloom downsample effect
	std::unique_ptr<d3d11_effect> m_vector_effect;          // pointer to the vector-effect object
	std::unique_ptr<d3d11_effect> m_chroma_effect;

	d3d11_texture_info *          m_diffuse_texture;
	bool                          m_filter_screens;
	d3d11_texture_info *          m_curr_texture;
	d3d11_render_target *         m_curr_render_target;
	d3d11_poly_info *             m_curr_poly;

	std::vector<std::unique_ptr<d3d11_render_target>> m_render_target_list;

	std::vector<std::unique_ptr<d3d11_slider>>        m_internal_sliders;
	std::vector<ui::menu_item>                        m_sliders;
	std::vector<std::unique_ptr<slider_state>>  m_core_sliders;

	static d3d11_slider_desc  s_sliders[];
	static d3d11_hlsl_options s_last_options;             // last used options
	static char               s_last_system_name[16];     // last used system
};

#endif
