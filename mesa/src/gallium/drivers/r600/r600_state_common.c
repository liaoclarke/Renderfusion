/*
 * Copyright 2010 Red Hat Inc.
 *           2010 Jerome Glisse
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie <airlied@redhat.com>
 *          Jerome Glisse <jglisse@redhat.com>
 */
#include "r600_formats.h"
#include "r600d.h"

#include "util/u_draw_quad.h"
#include "util/u_upload_mgr.h"
#include "tgsi/tgsi_parse.h"
#include <byteswap.h>

#define R600_PRIM_RECTANGLE_LIST PIPE_PRIM_MAX

static void r600_emit_command_buffer(struct r600_context *rctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = rctx->cs;
	struct r600_command_buffer *cb = (struct r600_command_buffer*)atom;

	assert(cs->cdw + cb->atom.num_dw <= RADEON_MAX_CMDBUF_DWORDS);
	memcpy(cs->buf + cs->cdw, cb->buf, 4 * cb->atom.num_dw);
	cs->cdw += cb->atom.num_dw;
}

void r600_init_command_buffer(struct r600_context *rctx, struct r600_command_buffer *cb, unsigned id, unsigned num_dw)
{
	r600_init_atom(rctx, &cb->atom, id, r600_emit_command_buffer, 0);
	cb->buf = CALLOC(1, 4 * num_dw);
	cb->max_num_dw = num_dw;
}

void r600_release_command_buffer(struct r600_command_buffer *cb)
{
	FREE(cb->buf);
}

void r600_init_atom(struct r600_context *rctx,
		    struct r600_atom *atom,
		    unsigned id,
		    void (*emit)(struct r600_context *ctx, struct r600_atom *state),
		    unsigned num_dw)
{
	assert(id < R600_NUM_ATOMS);
	assert(rctx->atoms[id] == NULL);
	rctx->atoms[id] = atom;
	atom->id = id;
	atom->emit = emit;
	atom->num_dw = num_dw;
	atom->dirty = false;
}

void r600_emit_alphatest_state(struct r600_context *rctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = rctx->cs;
	struct r600_alphatest_state *a = (struct r600_alphatest_state*)atom;
	unsigned alpha_ref = a->sx_alpha_ref;

	if (rctx->chip_class >= EVERGREEN && a->cb0_export_16bpc) {
		alpha_ref &= ~0x1FFF;
	}

	r600_write_context_reg(cs, R_028410_SX_ALPHA_TEST_CONTROL,
			       a->sx_alpha_test_control |
			       S_028410_ALPHA_TEST_BYPASS(a->bypass));
	r600_write_context_reg(cs, R_028438_SX_ALPHA_REF, alpha_ref);
}

static void r600_texture_barrier(struct pipe_context *ctx)
{
	struct r600_context *rctx = (struct r600_context *)ctx;

	rctx->flags |= R600_CONTEXT_CB_FLUSH | R600_CONTEXT_TEX_FLUSH;

	/* R6xx errata */
	if (rctx->chip_class == R600) {
		rctx->flags |= R600_CONTEXT_FLUSH_AND_INV;
	}
}

static unsigned r600_conv_pipe_prim(unsigned prim)
{
	static const unsigned prim_conv[] = {
		V_008958_DI_PT_POINTLIST,
		V_008958_DI_PT_LINELIST,
		V_008958_DI_PT_LINELOOP,
		V_008958_DI_PT_LINESTRIP,
		V_008958_DI_PT_TRILIST,
		V_008958_DI_PT_TRISTRIP,
		V_008958_DI_PT_TRIFAN,
		V_008958_DI_PT_QUADLIST,
		V_008958_DI_PT_QUADSTRIP,
		V_008958_DI_PT_POLYGON,
		V_008958_DI_PT_LINELIST_ADJ,
		V_008958_DI_PT_LINESTRIP_ADJ,
		V_008958_DI_PT_TRILIST_ADJ,
		V_008958_DI_PT_TRISTRIP_ADJ,
		V_008958_DI_PT_RECTLIST
	};
	return prim_conv[prim];
}

/* common state between evergreen and r600 */

static void r600_bind_blend_state_internal(struct r600_context *rctx,
		struct r600_pipe_blend *blend)
{
	struct r600_pipe_state *rstate;
	bool update_cb = false;

	rstate = &blend->rstate;
	rctx->states[rstate->id] = rstate;
	r600_context_pipe_state_set(rctx, rstate);

	if (rctx->cb_misc_state.blend_colormask != blend->cb_target_mask) {
		rctx->cb_misc_state.blend_colormask = blend->cb_target_mask;
		update_cb = true;
	}
	if (rctx->chip_class <= R700 &&
	    rctx->cb_misc_state.cb_color_control != blend->cb_color_control) {
		rctx->cb_misc_state.cb_color_control = blend->cb_color_control;
		update_cb = true;
	}
	if (rctx->cb_misc_state.dual_src_blend != blend->dual_src_blend) {
		rctx->cb_misc_state.dual_src_blend = blend->dual_src_blend;
		update_cb = true;
	}
	if (update_cb) {
		r600_atom_dirty(rctx, &rctx->cb_misc_state.atom);
	}
}

static void r600_bind_blend_state(struct pipe_context *ctx, void *state)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	struct r600_pipe_blend *blend = (struct r600_pipe_blend *)state;

	if (blend == NULL)
		return;

	rctx->blend = blend;
	rctx->alpha_to_one = blend->alpha_to_one;
	rctx->dual_src_blend = blend->dual_src_blend;

	if (!rctx->blend_override)
		r600_bind_blend_state_internal(rctx, blend);
}

static void r600_set_blend_color(struct pipe_context *ctx,
				 const struct pipe_blend_color *state)
{
	struct r600_context *rctx = (struct r600_context *)ctx;

	rctx->blend_color.state = *state;
	r600_atom_dirty(rctx, &rctx->blend_color.atom);
}

void r600_emit_blend_color(struct r600_context *rctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = rctx->cs;
	struct pipe_blend_color *state = &rctx->blend_color.state;

	r600_write_context_reg_seq(cs, R_028414_CB_BLEND_RED, 4);
	r600_write_value(cs, fui(state->color[0])); /* R_028414_CB_BLEND_RED */
	r600_write_value(cs, fui(state->color[1])); /* R_028418_CB_BLEND_GREEN */
	r600_write_value(cs, fui(state->color[2])); /* R_02841C_CB_BLEND_BLUE */
	r600_write_value(cs, fui(state->color[3])); /* R_028420_CB_BLEND_ALPHA */
}

void r600_emit_vgt_state(struct r600_context *rctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = rctx->cs;
	struct r600_vgt_state *a = (struct r600_vgt_state *)atom;

	r600_write_context_reg(cs, R_028A94_VGT_MULTI_PRIM_IB_RESET_EN, a->vgt_multi_prim_ib_reset_en);
	r600_write_context_reg(cs, R_02840C_VGT_MULTI_PRIM_IB_RESET_INDX, a->vgt_multi_prim_ib_reset_indx);
}

void r600_emit_vgt2_state(struct r600_context *rctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = rctx->cs;
	struct r600_vgt2_state *a = (struct r600_vgt2_state *)atom;

	r600_write_context_reg(cs, R_028408_VGT_INDX_OFFSET, a->vgt_indx_offset);
}

static void r600_set_clip_state(struct pipe_context *ctx,
				const struct pipe_clip_state *state)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	struct pipe_constant_buffer cb;

	rctx->clip_state.state = *state;
	r600_atom_dirty(rctx, &rctx->clip_state.atom);

	cb.buffer = NULL;
	cb.user_buffer = state->ucp;
	cb.buffer_offset = 0;
	cb.buffer_size = 4*4*8;
	ctx->set_constant_buffer(ctx, PIPE_SHADER_VERTEX, 1, &cb);
	pipe_resource_reference(&cb.buffer, NULL);
}

static void r600_set_stencil_ref(struct pipe_context *ctx,
				 const struct r600_stencil_ref *state)
{
	struct r600_context *rctx = (struct r600_context *)ctx;

	rctx->stencil_ref.state = *state;
	r600_atom_dirty(rctx, &rctx->stencil_ref.atom);
}

void r600_emit_stencil_ref(struct r600_context *rctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = rctx->cs;
	struct r600_stencil_ref_state *a = (struct r600_stencil_ref_state*)atom;

	r600_write_context_reg_seq(cs, R_028430_DB_STENCILREFMASK, 2);
	r600_write_value(cs, /* R_028430_DB_STENCILREFMASK */
			 S_028430_STENCILREF(a->state.ref_value[0]) |
			 S_028430_STENCILMASK(a->state.valuemask[0]) |
			 S_028430_STENCILWRITEMASK(a->state.writemask[0]));
	r600_write_value(cs, /* R_028434_DB_STENCILREFMASK_BF */
			 S_028434_STENCILREF_BF(a->state.ref_value[1]) |
			 S_028434_STENCILMASK_BF(a->state.valuemask[1]) |
			 S_028434_STENCILWRITEMASK_BF(a->state.writemask[1]));
}

static void r600_set_pipe_stencil_ref(struct pipe_context *ctx,
				      const struct pipe_stencil_ref *state)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	struct r600_pipe_dsa *dsa = (struct r600_pipe_dsa*)rctx->states[R600_PIPE_STATE_DSA];
	struct r600_stencil_ref ref;

	rctx->stencil_ref.pipe_state = *state;

	if (!dsa)
		return;

	ref.ref_value[0] = state->ref_value[0];
	ref.ref_value[1] = state->ref_value[1];
	ref.valuemask[0] = dsa->valuemask[0];
	ref.valuemask[1] = dsa->valuemask[1];
	ref.writemask[0] = dsa->writemask[0];
	ref.writemask[1] = dsa->writemask[1];

	r600_set_stencil_ref(ctx, &ref);
}

static void r600_bind_dsa_state(struct pipe_context *ctx, void *state)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	struct r600_pipe_dsa *dsa = state;
	struct r600_pipe_state *rstate;
	struct r600_stencil_ref ref;

	if (state == NULL)
		return;
	rstate = &dsa->rstate;
	rctx->states[rstate->id] = rstate;
	r600_context_pipe_state_set(rctx, rstate);

	ref.ref_value[0] = rctx->stencil_ref.pipe_state.ref_value[0];
	ref.ref_value[1] = rctx->stencil_ref.pipe_state.ref_value[1];
	ref.valuemask[0] = dsa->valuemask[0];
	ref.valuemask[1] = dsa->valuemask[1];
	ref.writemask[0] = dsa->writemask[0];
	ref.writemask[1] = dsa->writemask[1];

	r600_set_stencil_ref(ctx, &ref);

	/* Update alphatest state. */
	if (rctx->alphatest_state.sx_alpha_test_control != dsa->sx_alpha_test_control ||
	    rctx->alphatest_state.sx_alpha_ref != dsa->alpha_ref) {
		rctx->alphatest_state.sx_alpha_test_control = dsa->sx_alpha_test_control;
		rctx->alphatest_state.sx_alpha_ref = dsa->alpha_ref;
		r600_atom_dirty(rctx, &rctx->alphatest_state.atom);
	}
}

void r600_set_max_scissor(struct r600_context *rctx)
{
	/* Set a scissor state such that it doesn't do anything. */
	struct pipe_scissor_state scissor;
	scissor.minx = 0;
	scissor.miny = 0;
	scissor.maxx = 8192;
	scissor.maxy = 8192;

	r600_set_scissor_state(rctx, &scissor);
}

static void r600_bind_rs_state(struct pipe_context *ctx, void *state)
{
	struct r600_pipe_rasterizer *rs = (struct r600_pipe_rasterizer *)state;
	struct r600_context *rctx = (struct r600_context *)ctx;

	if (state == NULL)
		return;

	rctx->sprite_coord_enable = rs->sprite_coord_enable;
	rctx->two_side = rs->two_side;
	rctx->pa_sc_line_stipple = rs->pa_sc_line_stipple;
	rctx->multisample_enable = rs->multisample_enable;

	rctx->rasterizer = rs;

	rctx->states[rs->rstate.id] = &rs->rstate;
	r600_context_pipe_state_set(rctx, &rs->rstate);

	if (rctx->chip_class >= EVERGREEN) {
		evergreen_polygon_offset_update(rctx);
	} else {
		r600_polygon_offset_update(rctx);
	}

	/* Update clip_misc_state. */
	if (rctx->clip_misc_state.pa_cl_clip_cntl != rs->pa_cl_clip_cntl ||
	    rctx->clip_misc_state.clip_plane_enable != rs->clip_plane_enable) {
		rctx->clip_misc_state.pa_cl_clip_cntl = rs->pa_cl_clip_cntl;
		rctx->clip_misc_state.clip_plane_enable = rs->clip_plane_enable;
		r600_atom_dirty(rctx, &rctx->clip_misc_state.atom);
	}

	/* Workaround for a missing scissor enable on r600. */
	if (rctx->chip_class == R600) {
		if (rs->scissor_enable != rctx->scissor_enable) {
			rctx->scissor_enable = rs->scissor_enable;

			if (rs->scissor_enable) {
				r600_set_scissor_state(rctx, &rctx->scissor);
			} else {
				r600_set_max_scissor(rctx);
			}
		}
	}
}

static void r600_delete_rs_state(struct pipe_context *ctx, void *state)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	struct r600_pipe_rasterizer *rs = (struct r600_pipe_rasterizer *)state;

	if (rctx->rasterizer == rs) {
		rctx->rasterizer = NULL;
	}
	if (rctx->states[rs->rstate.id] == &rs->rstate) {
		rctx->states[rs->rstate.id] = NULL;
	}
	free(rs);
}

static void r600_sampler_view_destroy(struct pipe_context *ctx,
				      struct pipe_sampler_view *state)
{
	struct r600_pipe_sampler_view *resource = (struct r600_pipe_sampler_view *)state;

	pipe_resource_reference(&state->texture, NULL);
	FREE(resource);
}

void r600_sampler_states_dirty(struct r600_context *rctx,
			       struct r600_sampler_states *state)
{
	if (state->dirty_mask) {
		if (state->dirty_mask & state->has_bordercolor_mask) {
			rctx->flags |= R600_CONTEXT_PS_PARTIAL_FLUSH;
		}
		state->atom.num_dw =
			util_bitcount(state->dirty_mask & state->has_bordercolor_mask) * 11 +
			util_bitcount(state->dirty_mask & ~state->has_bordercolor_mask) * 5;
		r600_atom_dirty(rctx, &state->atom);
	}
}

static void r600_bind_sampler_states(struct pipe_context *pipe,
                               unsigned shader,
			       unsigned start,
			       unsigned count, void **states)
{
	struct r600_context *rctx = (struct r600_context *)pipe;
	struct r600_textures_info *dst = &rctx->samplers[shader];
	struct r600_pipe_sampler_state **rstates = (struct r600_pipe_sampler_state**)states;
	int seamless_cube_map = -1;
	unsigned i;
	/* This sets 1-bit for states with index >= count. */
	uint32_t disable_mask = ~((1ull << count) - 1);
	/* These are the new states set by this function. */
	uint32_t new_mask = 0;

	assert(start == 0); /* XXX fix below */

	for (i = 0; i < count; i++) {
		struct r600_pipe_sampler_state *rstate = rstates[i];

		if (rstate == dst->states.states[i]) {
			continue;
		}

		if (rstate) {
			if (rstate->border_color_use) {
				dst->states.has_bordercolor_mask |= 1 << i;
			} else {
				dst->states.has_bordercolor_mask &= ~(1 << i);
			}
			seamless_cube_map = rstate->seamless_cube_map;

			new_mask |= 1 << i;
		} else {
			disable_mask |= 1 << i;
		}
	}

	memcpy(dst->states.states, rstates, sizeof(void*) * count);
	memset(dst->states.states + count, 0, sizeof(void*) * (NUM_TEX_UNITS - count));

	dst->states.enabled_mask &= ~disable_mask;
	dst->states.dirty_mask &= dst->states.enabled_mask;
	dst->states.enabled_mask |= new_mask;
	dst->states.dirty_mask |= new_mask;
	dst->states.has_bordercolor_mask &= dst->states.enabled_mask;

	r600_sampler_states_dirty(rctx, &dst->states);

	/* Seamless cubemap state. */
	if (rctx->chip_class <= R700 &&
	    seamless_cube_map != -1 &&
	    seamless_cube_map != rctx->seamless_cube_map.enabled) {
		/* change in TA_CNTL_AUX need a pipeline flush */
		rctx->flags |= R600_CONTEXT_PS_PARTIAL_FLUSH;
		rctx->seamless_cube_map.enabled = seamless_cube_map;
		r600_atom_dirty(rctx, &rctx->seamless_cube_map.atom);
	}
}

static void r600_bind_vs_sampler_states(struct pipe_context *ctx, unsigned count, void **states)
{
	r600_bind_sampler_states(ctx, PIPE_SHADER_VERTEX, 0, count, states);
}

static void r600_bind_ps_sampler_states(struct pipe_context *ctx, unsigned count, void **states)
{
	r600_bind_sampler_states(ctx, PIPE_SHADER_FRAGMENT, 0, count, states);
}

static void r600_delete_sampler_state(struct pipe_context *ctx, void *state)
{
	free(state);
}

static void r600_delete_state(struct pipe_context *ctx, void *state)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	struct r600_pipe_state *rstate = (struct r600_pipe_state *)state;

	if (rctx->states[rstate->id] == rstate) {
		rctx->states[rstate->id] = NULL;
	}
	for (int i = 0; i < rstate->nregs; i++) {
		pipe_resource_reference((struct pipe_resource**)&rstate->regs[i].bo, NULL);
	}
	free(rstate);
}

static void r600_bind_vertex_elements(struct pipe_context *ctx, void *state)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	struct r600_vertex_element *v = (struct r600_vertex_element*)state;

	rctx->vertex_elements = v;
	if (v) {
		rctx->states[v->rstate.id] = &v->rstate;
		r600_context_pipe_state_set(rctx, &v->rstate);
	}
}

static void r600_delete_vertex_elements(struct pipe_context *ctx, void *state)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	struct r600_vertex_element *v = (struct r600_vertex_element*)state;

	if (rctx->states[v->rstate.id] == &v->rstate) {
		rctx->states[v->rstate.id] = NULL;
	}
	if (rctx->vertex_elements == state)
		rctx->vertex_elements = NULL;

	pipe_resource_reference((struct pipe_resource**)&v->fetch_shader, NULL);
	FREE(state);
}

static void r600_set_index_buffer(struct pipe_context *ctx,
			   const struct pipe_index_buffer *ib)
{
	struct r600_context *rctx = (struct r600_context *)ctx;

	if (ib) {
		pipe_resource_reference(&rctx->index_buffer.buffer, ib->buffer);
	        memcpy(&rctx->index_buffer, ib, sizeof(*ib));
	} else {
		pipe_resource_reference(&rctx->index_buffer.buffer, NULL);
	}
}

void r600_vertex_buffers_dirty(struct r600_context *rctx)
{
	if (rctx->vertex_buffer_state.dirty_mask) {
		rctx->flags |= rctx->has_vertex_cache ? R600_CONTEXT_VTX_FLUSH : R600_CONTEXT_TEX_FLUSH;
		rctx->vertex_buffer_state.atom.num_dw = (rctx->chip_class >= EVERGREEN ? 12 : 11) *
					       util_bitcount(rctx->vertex_buffer_state.dirty_mask);
		r600_atom_dirty(rctx, &rctx->vertex_buffer_state.atom);
	}
}

static void r600_set_vertex_buffers(struct pipe_context *ctx, unsigned count,
			     const struct pipe_vertex_buffer *input)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	struct r600_vertexbuf_state *state = &rctx->vertex_buffer_state;
	struct pipe_vertex_buffer *vb = state->vb;
	unsigned i;
	/* This sets 1-bit for buffers with index >= count. */
	uint32_t disable_mask = ~((1ull << count) - 1);
	/* These are the new buffers set by this function. */
	uint32_t new_buffer_mask = 0;

	/* Set buffers with index >= count to NULL. */
	uint32_t remaining_buffers_mask =
		rctx->vertex_buffer_state.enabled_mask & disable_mask;

	while (remaining_buffers_mask) {
		i = u_bit_scan(&remaining_buffers_mask);
		pipe_resource_reference(&vb[i].buffer, NULL);
	}

	/* Set vertex buffers. */
	for (i = 0; i < count; i++) {
		if (memcmp(&input[i], &vb[i], sizeof(struct pipe_vertex_buffer))) {
			if (input[i].buffer) {
				vb[i].stride = input[i].stride;
				vb[i].buffer_offset = input[i].buffer_offset;
				pipe_resource_reference(&vb[i].buffer, input[i].buffer);
				new_buffer_mask |= 1 << i;
			} else {
				pipe_resource_reference(&vb[i].buffer, NULL);
				disable_mask |= 1 << i;
			}
		}
        }

	rctx->vertex_buffer_state.enabled_mask &= ~disable_mask;
	rctx->vertex_buffer_state.dirty_mask &= rctx->vertex_buffer_state.enabled_mask;
	rctx->vertex_buffer_state.enabled_mask |= new_buffer_mask;
	rctx->vertex_buffer_state.dirty_mask |= new_buffer_mask;

	r600_vertex_buffers_dirty(rctx);
}

void r600_sampler_views_dirty(struct r600_context *rctx,
			      struct r600_samplerview_state *state)
{
	if (state->dirty_mask) {
		rctx->flags |= R600_CONTEXT_TEX_FLUSH;
		state->atom.num_dw = (rctx->chip_class >= EVERGREEN ? 14 : 13) *
				     util_bitcount(state->dirty_mask);
		r600_atom_dirty(rctx, &state->atom);
	}
}

static void r600_set_sampler_views(struct pipe_context *pipe, unsigned shader,
				   unsigned start, unsigned count,
				   struct pipe_sampler_view **views)
{
	struct r600_context *rctx = (struct r600_context *) pipe;
	struct r600_textures_info *dst = &rctx->samplers[shader];
	struct r600_pipe_sampler_view **rviews = (struct r600_pipe_sampler_view **)views;
	uint32_t dirty_sampler_states_mask = 0;
	unsigned i;
	/* This sets 1-bit for textures with index >= count. */
	uint32_t disable_mask = ~((1ull << count) - 1);
	/* These are the new textures set by this function. */
	uint32_t new_mask = 0;

	/* Set textures with index >= count to NULL. */
	uint32_t remaining_mask;

	assert(start == 0); /* XXX fix below */

	remaining_mask = dst->views.enabled_mask & disable_mask;

	while (remaining_mask) {
		i = u_bit_scan(&remaining_mask);
		assert(dst->views.views[i]);

		pipe_sampler_view_reference((struct pipe_sampler_view **)&dst->views.views[i], NULL);
	}

	for (i = 0; i < count; i++) {
		if (rviews[i] == dst->views.views[i]) {
			continue;
		}

		if (rviews[i]) {
			struct r600_texture *rtex =
				(struct r600_texture*)rviews[i]->base.texture;

			if (rtex->is_depth && !rtex->is_flushing_texture) {
				dst->views.compressed_depthtex_mask |= 1 << i;
			} else {
				dst->views.compressed_depthtex_mask &= ~(1 << i);
			}

			/* Track compressed colorbuffers for Evergreen (Cayman doesn't need this). */
			if (rctx->chip_class != CAYMAN && rtex->cmask_size && rtex->fmask_size) {
				dst->views.compressed_colortex_mask |= 1 << i;
			} else {
				dst->views.compressed_colortex_mask &= ~(1 << i);
			}

			/* Changing from array to non-arrays textures and vice versa requires
			 * updating TEX_ARRAY_OVERRIDE in sampler states on R6xx-R7xx. */
			if (rctx->chip_class <= R700 &&
			    (dst->states.enabled_mask & (1 << i)) &&
			    (rviews[i]->base.texture->target == PIPE_TEXTURE_1D_ARRAY ||
			     rviews[i]->base.texture->target == PIPE_TEXTURE_2D_ARRAY) != dst->is_array_sampler[i]) {
				dirty_sampler_states_mask |= 1 << i;
			}

			pipe_sampler_view_reference((struct pipe_sampler_view **)&dst->views.views[i], views[i]);
			new_mask |= 1 << i;
		} else {
			pipe_sampler_view_reference((struct pipe_sampler_view **)&dst->views.views[i], NULL);
			disable_mask |= 1 << i;
		}
	}

	dst->views.enabled_mask &= ~disable_mask;
	dst->views.dirty_mask &= dst->views.enabled_mask;
	dst->views.enabled_mask |= new_mask;
	dst->views.dirty_mask |= new_mask;
	dst->views.compressed_depthtex_mask &= dst->views.enabled_mask;
	dst->views.compressed_colortex_mask &= dst->views.enabled_mask;

	r600_sampler_views_dirty(rctx, &dst->views);

	if (dirty_sampler_states_mask) {
		dst->states.dirty_mask |= dirty_sampler_states_mask;
		r600_sampler_states_dirty(rctx, &dst->states);
	}
}

static void r600_set_vs_sampler_views(struct pipe_context *ctx, unsigned count,
				      struct pipe_sampler_view **views)
{
	r600_set_sampler_views(ctx, PIPE_SHADER_VERTEX, 0, count, views);
}

static void r600_set_ps_sampler_views(struct pipe_context *ctx, unsigned count,
				      struct pipe_sampler_view **views)
{
	r600_set_sampler_views(ctx, PIPE_SHADER_FRAGMENT, 0, count, views);
}

static void r600_set_viewport_state(struct pipe_context *ctx,
				    const struct pipe_viewport_state *state)
{
	struct r600_context *rctx = (struct r600_context *)ctx;

	rctx->viewport.state = *state;
	r600_atom_dirty(rctx, &rctx->viewport.atom);
}

void r600_emit_viewport_state(struct r600_context *rctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = rctx->cs;
	struct pipe_viewport_state *state = &rctx->viewport.state;

	r600_write_context_reg_seq(cs, R_02843C_PA_CL_VPORT_XSCALE_0, 6);
	r600_write_value(cs, fui(state->scale[0]));     /* R_02843C_PA_CL_VPORT_XSCALE_0  */
	r600_write_value(cs, fui(state->translate[0])); /* R_028440_PA_CL_VPORT_XOFFSET_0 */
	r600_write_value(cs, fui(state->scale[1]));     /* R_028444_PA_CL_VPORT_YSCALE_0  */
	r600_write_value(cs, fui(state->translate[1])); /* R_028448_PA_CL_VPORT_YOFFSET_0 */
	r600_write_value(cs, fui(state->scale[2]));     /* R_02844C_PA_CL_VPORT_ZSCALE_0  */
	r600_write_value(cs, fui(state->translate[2])); /* R_028450_PA_CL_VPORT_ZOFFSET_0 */
}

static void *r600_create_vertex_elements(struct pipe_context *ctx, unsigned count,
					 const struct pipe_vertex_element *elements)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	struct r600_vertex_element *v = CALLOC_STRUCT(r600_vertex_element);

	assert(count < 32);
	if (!v)
		return NULL;

	v->count = count;
	memcpy(v->elements, elements, sizeof(struct pipe_vertex_element) * count);

	if (r600_vertex_elements_build_fetch_shader(rctx, v)) {
		FREE(v);
		return NULL;
	}

	return v;
}

/* Compute the key for the hw shader variant */
static INLINE struct r600_shader_key r600_shader_selector_key(struct pipe_context * ctx,
		struct r600_pipe_shader_selector * sel)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	struct r600_shader_key key;
	memset(&key, 0, sizeof(key));

	if (sel->type == PIPE_SHADER_FRAGMENT) {
		key.color_two_side = rctx->two_side;
		key.alpha_to_one = rctx->alpha_to_one &&
				   rctx->multisample_enable &&
				   !rctx->framebuffer.cb0_is_integer;
		key.dual_src_blend = rctx->dual_src_blend;
		key.nr_cbufs = rctx->framebuffer.state.nr_cbufs;
	}
	return key;
}

/* Select the hw shader variant depending on the current state.
 * (*dirty) is set to 1 if current variant was changed */
static int r600_shader_select(struct pipe_context *ctx,
        struct r600_pipe_shader_selector* sel,
        unsigned *dirty)
{
	struct r600_shader_key key;
	struct r600_context *rctx = (struct r600_context *)ctx;
	struct r600_pipe_shader * shader = NULL;
	int r;

	key = r600_shader_selector_key(ctx, sel);

	/* Check if we don't need to change anything.
	 * This path is also used for most shaders that don't need multiple
	 * variants, it will cost just a computation of the key and this
	 * test. */
	if (likely(sel->current && memcmp(&sel->current->key, &key, sizeof(key)) == 0)) {
		return 0;
	}

	/* lookup if we have other variants in the list */
	if (sel->num_shaders > 1) {
		struct r600_pipe_shader *p = sel->current, *c = p->next_variant;

		while (c && memcmp(&c->key, &key, sizeof(key)) != 0) {
			p = c;
			c = c->next_variant;
		}

		if (c) {
			p->next_variant = c->next_variant;
			shader = c;
		}
	}

	if (unlikely(!shader)) {
		shader = CALLOC(1, sizeof(struct r600_pipe_shader));
		shader->selector = sel;

		r = r600_pipe_shader_create(ctx, shader, key);
		if (unlikely(r)) {
			R600_ERR("Failed to build shader variant (type=%u) %d\n",
				 sel->type, r);
			sel->current = NULL;
			return r;
		}

		/* We don't know the value of nr_ps_max_color_exports until we built
		 * at least one variant, so we may need to recompute the key after
		 * building first variant. */
		if (sel->type == PIPE_SHADER_FRAGMENT &&
				sel->num_shaders == 0) {
			sel->nr_ps_max_color_exports = shader->shader.nr_ps_max_color_exports;
			key = r600_shader_selector_key(ctx, sel);
		}

		shader->key = key;
		sel->num_shaders++;
	}

	if (dirty)
		*dirty = 1;

	shader->next_variant = sel->current;
	sel->current = shader;

	if (rctx->chip_class < EVERGREEN && rctx->ps_shader && rctx->vs_shader) {
		r600_adjust_gprs(rctx);
	}

	if (rctx->ps_shader &&
	    rctx->cb_misc_state.nr_ps_color_outputs != rctx->ps_shader->current->nr_ps_color_outputs) {
		rctx->cb_misc_state.nr_ps_color_outputs = rctx->ps_shader->current->nr_ps_color_outputs;
		r600_atom_dirty(rctx, &rctx->cb_misc_state.atom);
	}
	return 0;
}

static void *r600_create_shader_state(struct pipe_context *ctx,
			       const struct pipe_shader_state *state,
			       unsigned pipe_shader_type)
{
	struct r600_pipe_shader_selector *sel = CALLOC_STRUCT(r600_pipe_shader_selector);
	int r;

	sel->type = pipe_shader_type;
	sel->tokens = tgsi_dup_tokens(state->tokens);
	sel->so = state->stream_output;

	r = r600_shader_select(ctx, sel, NULL);
	if (r)
	    return NULL;

	return sel;
}

static void *r600_create_ps_state(struct pipe_context *ctx,
					 const struct pipe_shader_state *state)
{
	return r600_create_shader_state(ctx, state, PIPE_SHADER_FRAGMENT);
}

static void *r600_create_vs_state(struct pipe_context *ctx,
					 const struct pipe_shader_state *state)
{
	return r600_create_shader_state(ctx, state, PIPE_SHADER_VERTEX);
}

static void r600_bind_ps_state(struct pipe_context *ctx, void *state)
{
	struct r600_context *rctx = (struct r600_context *)ctx;

	if (!state)
		state = rctx->dummy_pixel_shader;

	rctx->ps_shader = (struct r600_pipe_shader_selector *)state;
	r600_context_pipe_state_set(rctx, &rctx->ps_shader->current->rstate);

	if (rctx->chip_class <= R700) {
		bool multiwrite = rctx->ps_shader->current->shader.fs_write_all;

		if (rctx->cb_misc_state.multiwrite != multiwrite) {
			rctx->cb_misc_state.multiwrite = multiwrite;
			r600_atom_dirty(rctx, &rctx->cb_misc_state.atom);
		}

		if (rctx->vs_shader)
			r600_adjust_gprs(rctx);
	}

	if (rctx->cb_misc_state.nr_ps_color_outputs != rctx->ps_shader->current->nr_ps_color_outputs) {
		rctx->cb_misc_state.nr_ps_color_outputs = rctx->ps_shader->current->nr_ps_color_outputs;
		r600_atom_dirty(rctx, &rctx->cb_misc_state.atom);
	}
}

static void r600_bind_vs_state(struct pipe_context *ctx, void *state)
{
	struct r600_context *rctx = (struct r600_context *)ctx;

	rctx->vs_shader = (struct r600_pipe_shader_selector *)state;
	if (state) {
		r600_context_pipe_state_set(rctx, &rctx->vs_shader->current->rstate);

		if (rctx->chip_class < EVERGREEN && rctx->ps_shader)
			r600_adjust_gprs(rctx);

		/* Update clip misc state. */
		if (rctx->vs_shader->current->pa_cl_vs_out_cntl != rctx->clip_misc_state.pa_cl_vs_out_cntl ||
		    rctx->vs_shader->current->shader.clip_dist_write != rctx->clip_misc_state.clip_dist_write) {
			rctx->clip_misc_state.pa_cl_vs_out_cntl = rctx->vs_shader->current->pa_cl_vs_out_cntl;
			rctx->clip_misc_state.clip_dist_write = rctx->vs_shader->current->shader.clip_dist_write;
			r600_atom_dirty(rctx, &rctx->clip_misc_state.atom);
		}
	}
}

static void r600_delete_shader_selector(struct pipe_context *ctx,
		struct r600_pipe_shader_selector *sel)
{
	struct r600_pipe_shader *p = sel->current, *c;
	while (p) {
		c = p->next_variant;
		r600_pipe_shader_destroy(ctx, p);
		free(p);
		p = c;
	}

	free(sel->tokens);
	free(sel);
}


static void r600_delete_ps_state(struct pipe_context *ctx, void *state)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	struct r600_pipe_shader_selector *sel = (struct r600_pipe_shader_selector *)state;

	if (rctx->ps_shader == sel) {
		rctx->ps_shader = NULL;
	}

	r600_delete_shader_selector(ctx, sel);
}

static void r600_delete_vs_state(struct pipe_context *ctx, void *state)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	struct r600_pipe_shader_selector *sel = (struct r600_pipe_shader_selector *)state;

	if (rctx->vs_shader == sel) {
		rctx->vs_shader = NULL;
	}

	r600_delete_shader_selector(ctx, sel);
}

void r600_constant_buffers_dirty(struct r600_context *rctx, struct r600_constbuf_state *state)
{
	if (state->dirty_mask) {
		rctx->flags |= R600_CONTEXT_SHADERCONST_FLUSH;
		state->atom.num_dw = rctx->chip_class >= EVERGREEN ? util_bitcount(state->dirty_mask)*20
								   : util_bitcount(state->dirty_mask)*19;
		r600_atom_dirty(rctx, &state->atom);
	}
}

static void r600_set_constant_buffer(struct pipe_context *ctx, uint shader, uint index,
				     struct pipe_constant_buffer *input)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	struct r600_constbuf_state *state = &rctx->constbuf_state[shader];
	struct pipe_constant_buffer *cb;
	const uint8_t *ptr;

	/* Note that the state tracker can unbind constant buffers by
	 * passing NULL here.
	 */
	if (unlikely(!input)) {
		state->enabled_mask &= ~(1 << index);
		state->dirty_mask &= ~(1 << index);
		pipe_resource_reference(&state->cb[index].buffer, NULL);
		return;
	}

	cb = &state->cb[index];
	cb->buffer_size = input->buffer_size;

	ptr = input->user_buffer;

	if (ptr) {
		/* Upload the user buffer. */
		if (R600_BIG_ENDIAN) {
			uint32_t *tmpPtr;
			unsigned i, size = input->buffer_size;

			if (!(tmpPtr = malloc(size))) {
				R600_ERR("Failed to allocate BE swap buffer.\n");
				return;
			}

			for (i = 0; i < size / 4; ++i) {
				tmpPtr[i] = bswap_32(((uint32_t *)ptr)[i]);
			}

			u_upload_data(rctx->uploader, 0, size, tmpPtr, &cb->buffer_offset, &cb->buffer);
			free(tmpPtr);
		} else {
			u_upload_data(rctx->uploader, 0, input->buffer_size, ptr, &cb->buffer_offset, &cb->buffer);
		}
	} else {
		/* Setup the hw buffer. */
		cb->buffer_offset = input->buffer_offset;
		pipe_resource_reference(&cb->buffer, input->buffer);
	}

	state->enabled_mask |= 1 << index;
	state->dirty_mask |= 1 << index;
	r600_constant_buffers_dirty(rctx, state);
}

static struct pipe_stream_output_target *
r600_create_so_target(struct pipe_context *ctx,
		      struct pipe_resource *buffer,
		      unsigned buffer_offset,
		      unsigned buffer_size)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	struct r600_so_target *t;
	void *ptr;

	t = CALLOC_STRUCT(r600_so_target);
	if (!t) {
		return NULL;
	}

	t->b.reference.count = 1;
	t->b.context = ctx;
	pipe_resource_reference(&t->b.buffer, buffer);
	t->b.buffer_offset = buffer_offset;
	t->b.buffer_size = buffer_size;

	t->filled_size = (struct r600_resource*)
		pipe_buffer_create(ctx->screen, PIPE_BIND_CUSTOM, PIPE_USAGE_STATIC, 4);
	ptr = rctx->ws->buffer_map(t->filled_size->cs_buf, rctx->cs, PIPE_TRANSFER_WRITE);
	memset(ptr, 0, t->filled_size->buf->size);
	rctx->ws->buffer_unmap(t->filled_size->cs_buf);

	return &t->b;
}

static void r600_so_target_destroy(struct pipe_context *ctx,
				   struct pipe_stream_output_target *target)
{
	struct r600_so_target *t = (struct r600_so_target*)target;
	pipe_resource_reference(&t->b.buffer, NULL);
	pipe_resource_reference((struct pipe_resource**)&t->filled_size, NULL);
	FREE(t);
}

static void r600_set_so_targets(struct pipe_context *ctx,
				unsigned num_targets,
				struct pipe_stream_output_target **targets,
				unsigned append_bitmask)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	unsigned i;

	/* Stop streamout. */
	if (rctx->num_so_targets && !rctx->streamout_start) {
		r600_context_streamout_end(rctx);
	}

	/* Set the new targets. */
	for (i = 0; i < num_targets; i++) {
		pipe_so_target_reference((struct pipe_stream_output_target**)&rctx->so_targets[i], targets[i]);
	}
	for (; i < rctx->num_so_targets; i++) {
		pipe_so_target_reference((struct pipe_stream_output_target**)&rctx->so_targets[i], NULL);
	}

	rctx->num_so_targets = num_targets;
	rctx->streamout_start = num_targets != 0;
	rctx->streamout_append_bitmask = append_bitmask;
}

static void r600_set_sample_mask(struct pipe_context *pipe, unsigned sample_mask)
{
	struct r600_context *rctx = (struct r600_context*)pipe;

	if (rctx->sample_mask.sample_mask == (uint16_t)sample_mask)
		return;

	rctx->sample_mask.sample_mask = sample_mask;
	r600_atom_dirty(rctx, &rctx->sample_mask.atom);
}

static void r600_update_derived_state(struct r600_context *rctx)
{
	struct pipe_context * ctx = (struct pipe_context*)rctx;
	unsigned ps_dirty = 0, blend_override;

	if (!rctx->blitter->running) {
		unsigned i;

		/* Decompress textures if needed. */
		for (i = 0; i < PIPE_SHADER_TYPES; i++) {
			struct r600_samplerview_state *views = &rctx->samplers[i].views;
			if (views->compressed_depthtex_mask) {
				r600_decompress_depth_textures(rctx, views);
			}
			if (views->compressed_colortex_mask) {
				r600_decompress_color_textures(rctx, views);
			}
		}
	}

	r600_shader_select(ctx, rctx->ps_shader, &ps_dirty);

	if (rctx->ps_shader && ((rctx->sprite_coord_enable &&
		(rctx->ps_shader->current->sprite_coord_enable != rctx->sprite_coord_enable)) ||
		(rctx->rasterizer && rctx->rasterizer->flatshade != rctx->ps_shader->current->flatshade))) {

		if (rctx->chip_class >= EVERGREEN)
			evergreen_pipe_shader_ps(ctx, rctx->ps_shader->current);
		else
			r600_pipe_shader_ps(ctx, rctx->ps_shader->current);

		ps_dirty = 1;
	}

	if (ps_dirty)
		r600_context_pipe_state_set(rctx, &rctx->ps_shader->current->rstate);

	blend_override = (rctx->dual_src_blend &&
			rctx->ps_shader->current->nr_ps_color_outputs < 2);

	if (blend_override != rctx->blend_override) {
		rctx->blend_override = blend_override;
		r600_bind_blend_state_internal(rctx,
				blend_override ? rctx->no_blend : rctx->blend);
	}

	if (rctx->chip_class >= EVERGREEN) {
		evergreen_update_dual_export_state(rctx);
	} else {
		r600_update_dual_export_state(rctx);
	}
}

static unsigned r600_conv_prim_to_gs_out(unsigned mode)
{
	static const int prim_conv[] = {
		V_028A6C_OUTPRIM_TYPE_POINTLIST,
		V_028A6C_OUTPRIM_TYPE_LINESTRIP,
		V_028A6C_OUTPRIM_TYPE_LINESTRIP,
		V_028A6C_OUTPRIM_TYPE_LINESTRIP,
		V_028A6C_OUTPRIM_TYPE_TRISTRIP,
		V_028A6C_OUTPRIM_TYPE_TRISTRIP,
		V_028A6C_OUTPRIM_TYPE_TRISTRIP,
		V_028A6C_OUTPRIM_TYPE_TRISTRIP,
		V_028A6C_OUTPRIM_TYPE_TRISTRIP,
		V_028A6C_OUTPRIM_TYPE_TRISTRIP,
		V_028A6C_OUTPRIM_TYPE_LINESTRIP,
		V_028A6C_OUTPRIM_TYPE_LINESTRIP,
		V_028A6C_OUTPRIM_TYPE_TRISTRIP,
		V_028A6C_OUTPRIM_TYPE_TRISTRIP,
		V_028A6C_OUTPRIM_TYPE_TRISTRIP
	};
	assert(mode < Elements(prim_conv));

	return prim_conv[mode];
}

void r600_emit_clip_misc_state(struct r600_context *rctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = rctx->cs;
	struct r600_clip_misc_state *state = &rctx->clip_misc_state;

	r600_write_context_reg(cs, R_028810_PA_CL_CLIP_CNTL,
			       state->pa_cl_clip_cntl |
			       (state->clip_dist_write ? 0 : state->clip_plane_enable & 0x3F));
	r600_write_context_reg(cs, R_02881C_PA_CL_VS_OUT_CNTL,
			       state->pa_cl_vs_out_cntl |
			       (state->clip_plane_enable & state->clip_dist_write));
}

static void r600_draw_vbo(struct pipe_context *ctx, const struct pipe_draw_info *dinfo)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	struct pipe_draw_info info = *dinfo;
	struct pipe_index_buffer ib = {};
	unsigned i;
	struct r600_block *dirty_block = NULL, *next_block = NULL;
	struct radeon_winsys_cs *cs = rctx->cs;
	uint64_t va;
	uint8_t *ptr;

	if (!info.count && (info.indexed || !info.count_from_stream_output)) {
		assert(0);
		return;
	}

	if (!rctx->vs_shader) {
		assert(0);
		return;
	}

	r600_update_derived_state(rctx);

	if (info.indexed) {
		/* Initialize the index buffer struct. */
		pipe_resource_reference(&ib.buffer, rctx->index_buffer.buffer);
		ib.user_buffer = rctx->index_buffer.user_buffer;
		ib.index_size = rctx->index_buffer.index_size;
		ib.offset = rctx->index_buffer.offset + info.start * ib.index_size;

		/* Translate or upload, if needed. */
		r600_translate_index_buffer(rctx, &ib, info.count);

		ptr = (uint8_t*)ib.user_buffer;
		if (!ib.buffer && ptr) {
			u_upload_data(rctx->uploader, 0, info.count * ib.index_size,
				      ptr, &ib.offset, &ib.buffer);
		}
	} else {
		info.index_bias = info.start;
	}

	/* Enable stream out if needed. */
	if (rctx->streamout_start) {
		r600_context_streamout_begin(rctx);
		rctx->streamout_start = FALSE;
	}

	/* Set the index offset and multi primitive */
	if (rctx->vgt2_state.vgt_indx_offset != info.index_bias) {
		rctx->vgt2_state.vgt_indx_offset = info.index_bias;
		r600_atom_dirty(rctx, &rctx->vgt2_state.atom);
	}
	if (rctx->vgt_state.vgt_multi_prim_ib_reset_en != info.primitive_restart ||
	    rctx->vgt_state.vgt_multi_prim_ib_reset_indx != info.restart_index) {
		rctx->vgt_state.vgt_multi_prim_ib_reset_en = info.primitive_restart;
		rctx->vgt_state.vgt_multi_prim_ib_reset_indx = info.restart_index;
		r600_atom_dirty(rctx, &rctx->vgt_state.atom);
	}

	/* Emit states (the function expects that we emit at most 17 dwords here). */
	r600_need_cs_space(rctx, 0, TRUE);
	r600_flush_emit(rctx);

	for (i = 0; i < R600_NUM_ATOMS; i++) {
		if (rctx->atoms[i] == NULL || !rctx->atoms[i]->dirty) {
			continue;
		}
		r600_emit_atom(rctx, rctx->atoms[i]);
	}
	LIST_FOR_EACH_ENTRY_SAFE(dirty_block, next_block, &rctx->dirty,list) {
		r600_context_block_emit_dirty(rctx, dirty_block, 0 /* pkt_flags */);
	}
	rctx->pm4_dirty_cdwords = 0;

	/* Update start instance. */
	if (rctx->last_start_instance != info.start_instance) {
		r600_write_ctl_const(cs, R_03CFF4_SQ_VTX_START_INST_LOC, info.start_instance);
		rctx->last_start_instance = info.start_instance;
	}

	/* Update the primitive type. */
	if (rctx->last_primitive_type != info.mode) {
		unsigned ls_mask = 0;

		if (info.mode == PIPE_PRIM_LINES)
			ls_mask = 1;
		else if (info.mode == PIPE_PRIM_LINE_STRIP ||
			 info.mode == PIPE_PRIM_LINE_LOOP)
			ls_mask = 2;

		r600_write_context_reg(cs, R_028A0C_PA_SC_LINE_STIPPLE,
				       S_028A0C_AUTO_RESET_CNTL(ls_mask) | rctx->pa_sc_line_stipple);
		r600_write_context_reg(cs, R_028A6C_VGT_GS_OUT_PRIM_TYPE,
				       r600_conv_prim_to_gs_out(info.mode));
		r600_write_config_reg(cs, R_008958_VGT_PRIMITIVE_TYPE,
				      r600_conv_pipe_prim(info.mode));

		rctx->last_primitive_type = info.mode;
	}

	/* Draw packets. */
	cs->buf[cs->cdw++] = PKT3(PKT3_NUM_INSTANCES, 0, rctx->predicate_drawing);
	cs->buf[cs->cdw++] = info.instance_count;
	if (info.indexed) {
		cs->buf[cs->cdw++] = PKT3(PKT3_INDEX_TYPE, 0, rctx->predicate_drawing);
		cs->buf[cs->cdw++] = ib.index_size == 4 ?
					(VGT_INDEX_32 | (R600_BIG_ENDIAN ? VGT_DMA_SWAP_32_BIT : 0)) :
					(VGT_INDEX_16 | (R600_BIG_ENDIAN ? VGT_DMA_SWAP_16_BIT : 0));

		va = r600_resource_va(ctx->screen, ib.buffer);
		va += ib.offset;
		cs->buf[cs->cdw++] = PKT3(PKT3_DRAW_INDEX, 3, rctx->predicate_drawing);
		cs->buf[cs->cdw++] = va;
		cs->buf[cs->cdw++] = (va >> 32UL) & 0xFF;
		cs->buf[cs->cdw++] = info.count;
		cs->buf[cs->cdw++] = V_0287F0_DI_SRC_SEL_DMA;
		cs->buf[cs->cdw++] = PKT3(PKT3_NOP, 0, rctx->predicate_drawing);
		cs->buf[cs->cdw++] = r600_context_bo_reloc(rctx, (struct r600_resource*)ib.buffer, RADEON_USAGE_READ);
	} else {
		if (info.count_from_stream_output) {
			struct r600_so_target *t = (struct r600_so_target*)info.count_from_stream_output;
			uint64_t va = r600_resource_va(&rctx->screen->screen, (void*)t->filled_size);

			r600_write_context_reg(cs, R_028B30_VGT_STRMOUT_DRAW_OPAQUE_VERTEX_STRIDE, t->stride_in_dw);

			cs->buf[cs->cdw++] = PKT3(PKT3_COPY_DW, 4, 0);
			cs->buf[cs->cdw++] = COPY_DW_SRC_IS_MEM | COPY_DW_DST_IS_REG;
			cs->buf[cs->cdw++] = va & 0xFFFFFFFFUL;     /* src address lo */
			cs->buf[cs->cdw++] = (va >> 32UL) & 0xFFUL; /* src address hi */
			cs->buf[cs->cdw++] = R_028B2C_VGT_STRMOUT_DRAW_OPAQUE_BUFFER_FILLED_SIZE >> 2; /* dst register */
			cs->buf[cs->cdw++] = 0; /* unused */

			cs->buf[cs->cdw++] = PKT3(PKT3_NOP, 0, 0);
			cs->buf[cs->cdw++] = r600_context_bo_reloc(rctx, t->filled_size, RADEON_USAGE_READ);
		}

		cs->buf[cs->cdw++] = PKT3(PKT3_DRAW_INDEX_AUTO, 1, rctx->predicate_drawing);
		cs->buf[cs->cdw++] = info.count;
		cs->buf[cs->cdw++] = V_0287F0_DI_SRC_SEL_AUTO_INDEX |
					(info.count_from_stream_output ? S_0287F0_USE_OPAQUE(1) : 0);
	}

	/* Set the depth buffer as dirty. */
	if (rctx->framebuffer.state.zsbuf) {
		struct pipe_surface *surf = rctx->framebuffer.state.zsbuf;
		struct r600_texture *rtex = (struct r600_texture *)surf->texture;

		rtex->dirty_level_mask |= 1 << surf->u.tex.level;
	}
	if (rctx->framebuffer.compressed_cb_mask) {
		struct pipe_surface *surf;
		struct r600_texture *rtex;
		unsigned mask = rctx->framebuffer.compressed_cb_mask;

		do {
			unsigned i = u_bit_scan(&mask);
			surf = rctx->framebuffer.state.cbufs[i];
			rtex = (struct r600_texture*)surf->texture;

			rtex->dirty_level_mask |= 1 << surf->u.tex.level;

		} while (mask);
	}

	pipe_resource_reference(&ib.buffer, NULL);
}

void r600_draw_rectangle(struct blitter_context *blitter,
			 int x1, int y1, int x2, int y2, float depth,
			 enum blitter_attrib_type type, const union pipe_color_union *attrib)
{
	struct r600_context *rctx = (struct r600_context*)util_blitter_get_pipe(blitter);
	struct pipe_viewport_state viewport;
	struct pipe_resource *buf = NULL;
	unsigned offset = 0;
	float *vb;

	if (type == UTIL_BLITTER_ATTRIB_TEXCOORD) {
		util_blitter_draw_rectangle(blitter, x1, y1, x2, y2, depth, type, attrib);
		return;
	}

	/* Some operations (like color resolve on r6xx) don't work
	 * with the conventional primitive types.
	 * One that works is PT_RECTLIST, which we use here. */

	/* setup viewport */
	viewport.scale[0] = 1.0f;
	viewport.scale[1] = 1.0f;
	viewport.scale[2] = 1.0f;
	viewport.scale[3] = 1.0f;
	viewport.translate[0] = 0.0f;
	viewport.translate[1] = 0.0f;
	viewport.translate[2] = 0.0f;
	viewport.translate[3] = 0.0f;
	rctx->context.set_viewport_state(&rctx->context, &viewport);

	/* Upload vertices. The hw rectangle has only 3 vertices,
	 * I guess the 4th one is derived from the first 3.
	 * The vertex specification should match u_blitter's vertex element state. */
	u_upload_alloc(rctx->uploader, 0, sizeof(float) * 24, &offset, &buf, (void**)&vb);
	vb[0] = x1;
	vb[1] = y1;
	vb[2] = depth;
	vb[3] = 1;

	vb[8] = x1;
	vb[9] = y2;
	vb[10] = depth;
	vb[11] = 1;

	vb[16] = x2;
	vb[17] = y1;
	vb[18] = depth;
	vb[19] = 1;

	if (attrib) {
		memcpy(vb+4, attrib->f, sizeof(float)*4);
		memcpy(vb+12, attrib->f, sizeof(float)*4);
		memcpy(vb+20, attrib->f, sizeof(float)*4);
	}

	/* draw */
	util_draw_vertex_buffer(&rctx->context, NULL, buf, offset,
				R600_PRIM_RECTANGLE_LIST, 3, 2);
	pipe_resource_reference(&buf, NULL);
}

void _r600_pipe_state_add_reg_bo(struct r600_context *ctx,
				 struct r600_pipe_state *state,
				 uint32_t offset, uint32_t value,
				 uint32_t range_id, uint32_t block_id,
				 struct r600_resource *bo,
				 enum radeon_bo_usage usage)
			      
{
	struct r600_range *range;
	struct r600_block *block;

	if (bo) assert(usage);

	range = &ctx->range[range_id];
	block = range->blocks[block_id];
	state->regs[state->nregs].block = block;
	state->regs[state->nregs].id = (offset - block->start_offset) >> 2;

	state->regs[state->nregs].value = value;
	state->regs[state->nregs].bo = bo;
	state->regs[state->nregs].bo_usage = usage;

	state->nregs++;
	assert(state->nregs < R600_BLOCK_MAX_REG);
}

void _r600_pipe_state_add_reg(struct r600_context *ctx,
			      struct r600_pipe_state *state,
			      uint32_t offset, uint32_t value,
			      uint32_t range_id, uint32_t block_id)
{
	_r600_pipe_state_add_reg_bo(ctx, state, offset, value,
				    range_id, block_id, NULL, 0);
}

uint32_t r600_translate_stencil_op(int s_op)
{
	switch (s_op) {
	case PIPE_STENCIL_OP_KEEP:
		return V_028800_STENCIL_KEEP;
	case PIPE_STENCIL_OP_ZERO:
		return V_028800_STENCIL_ZERO;
	case PIPE_STENCIL_OP_REPLACE:
		return V_028800_STENCIL_REPLACE;
	case PIPE_STENCIL_OP_INCR:
		return V_028800_STENCIL_INCR;
	case PIPE_STENCIL_OP_DECR:
		return V_028800_STENCIL_DECR;
	case PIPE_STENCIL_OP_INCR_WRAP:
		return V_028800_STENCIL_INCR_WRAP;
	case PIPE_STENCIL_OP_DECR_WRAP:
		return V_028800_STENCIL_DECR_WRAP;
	case PIPE_STENCIL_OP_INVERT:
		return V_028800_STENCIL_INVERT;
	default:
		R600_ERR("Unknown stencil op %d", s_op);
		assert(0);
		break;
	}
	return 0;
}

uint32_t r600_translate_fill(uint32_t func)
{
	switch(func) {
	case PIPE_POLYGON_MODE_FILL:
		return 2;
	case PIPE_POLYGON_MODE_LINE:
		return 1;
	case PIPE_POLYGON_MODE_POINT:
		return 0;
	default:
		assert(0);
		return 0;
	}
}

unsigned r600_tex_wrap(unsigned wrap)
{
	switch (wrap) {
	default:
	case PIPE_TEX_WRAP_REPEAT:
		return V_03C000_SQ_TEX_WRAP;
	case PIPE_TEX_WRAP_CLAMP:
		return V_03C000_SQ_TEX_CLAMP_HALF_BORDER;
	case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
		return V_03C000_SQ_TEX_CLAMP_LAST_TEXEL;
	case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
		return V_03C000_SQ_TEX_CLAMP_BORDER;
	case PIPE_TEX_WRAP_MIRROR_REPEAT:
		return V_03C000_SQ_TEX_MIRROR;
	case PIPE_TEX_WRAP_MIRROR_CLAMP:
		return V_03C000_SQ_TEX_MIRROR_ONCE_HALF_BORDER;
	case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE:
		return V_03C000_SQ_TEX_MIRROR_ONCE_LAST_TEXEL;
	case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER:
		return V_03C000_SQ_TEX_MIRROR_ONCE_BORDER;
	}
}

unsigned r600_tex_filter(unsigned filter)
{
	switch (filter) {
	default:
	case PIPE_TEX_FILTER_NEAREST:
		return V_03C000_SQ_TEX_XY_FILTER_POINT;
	case PIPE_TEX_FILTER_LINEAR:
		return V_03C000_SQ_TEX_XY_FILTER_BILINEAR;
	}
}

unsigned r600_tex_mipfilter(unsigned filter)
{
	switch (filter) {
	case PIPE_TEX_MIPFILTER_NEAREST:
		return V_03C000_SQ_TEX_Z_FILTER_POINT;
	case PIPE_TEX_MIPFILTER_LINEAR:
		return V_03C000_SQ_TEX_Z_FILTER_LINEAR;
	default:
	case PIPE_TEX_MIPFILTER_NONE:
		return V_03C000_SQ_TEX_Z_FILTER_NONE;
	}
}

unsigned r600_tex_compare(unsigned compare)
{
	switch (compare) {
	default:
	case PIPE_FUNC_NEVER:
		return V_03C000_SQ_TEX_DEPTH_COMPARE_NEVER;
	case PIPE_FUNC_LESS:
		return V_03C000_SQ_TEX_DEPTH_COMPARE_LESS;
	case PIPE_FUNC_EQUAL:
		return V_03C000_SQ_TEX_DEPTH_COMPARE_EQUAL;
	case PIPE_FUNC_LEQUAL:
		return V_03C000_SQ_TEX_DEPTH_COMPARE_LESSEQUAL;
	case PIPE_FUNC_GREATER:
		return V_03C000_SQ_TEX_DEPTH_COMPARE_GREATER;
	case PIPE_FUNC_NOTEQUAL:
		return V_03C000_SQ_TEX_DEPTH_COMPARE_NOTEQUAL;
	case PIPE_FUNC_GEQUAL:
		return V_03C000_SQ_TEX_DEPTH_COMPARE_GREATEREQUAL;
	case PIPE_FUNC_ALWAYS:
		return V_03C000_SQ_TEX_DEPTH_COMPARE_ALWAYS;
	}
}

/* keep this at the end of this file, please */
void r600_init_common_state_functions(struct r600_context *rctx)
{
	rctx->context.create_fs_state = r600_create_ps_state;
	rctx->context.create_vs_state = r600_create_vs_state;
	rctx->context.create_vertex_elements_state = r600_create_vertex_elements;
	rctx->context.bind_blend_state = r600_bind_blend_state;
	rctx->context.bind_depth_stencil_alpha_state = r600_bind_dsa_state;
	rctx->context.bind_fragment_sampler_states = r600_bind_ps_sampler_states;
	rctx->context.bind_fs_state = r600_bind_ps_state;
	rctx->context.bind_rasterizer_state = r600_bind_rs_state;
	rctx->context.bind_vertex_elements_state = r600_bind_vertex_elements;
	rctx->context.bind_vertex_sampler_states = r600_bind_vs_sampler_states;
	rctx->context.bind_vs_state = r600_bind_vs_state;
	rctx->context.delete_blend_state = r600_delete_state;
	rctx->context.delete_depth_stencil_alpha_state = r600_delete_state;
	rctx->context.delete_fs_state = r600_delete_ps_state;
	rctx->context.delete_rasterizer_state = r600_delete_rs_state;
	rctx->context.delete_sampler_state = r600_delete_sampler_state;
	rctx->context.delete_vertex_elements_state = r600_delete_vertex_elements;
	rctx->context.delete_vs_state = r600_delete_vs_state;
	rctx->context.set_blend_color = r600_set_blend_color;
	rctx->context.set_clip_state = r600_set_clip_state;
	rctx->context.set_constant_buffer = r600_set_constant_buffer;
	rctx->context.set_sample_mask = r600_set_sample_mask;
	rctx->context.set_stencil_ref = r600_set_pipe_stencil_ref;
	rctx->context.set_viewport_state = r600_set_viewport_state;
	rctx->context.set_vertex_buffers = r600_set_vertex_buffers;
	rctx->context.set_index_buffer = r600_set_index_buffer;
	rctx->context.set_fragment_sampler_views = r600_set_ps_sampler_views;
	rctx->context.set_vertex_sampler_views = r600_set_vs_sampler_views;
	rctx->context.sampler_view_destroy = r600_sampler_view_destroy;
	rctx->context.texture_barrier = r600_texture_barrier;
	rctx->context.create_stream_output_target = r600_create_so_target;
	rctx->context.stream_output_target_destroy = r600_so_target_destroy;
	rctx->context.set_stream_output_targets = r600_set_so_targets;
	rctx->context.draw_vbo = r600_draw_vbo;
}
