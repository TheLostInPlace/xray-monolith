-- external_static.s : lit + textured shader for external (GLTF/GLB) mesh geometry.
--
-- Renders external meshes through the engine's standard deferred MODEL path so they integrate
-- with scene lighting (sun/point/spot) and cast sun shadows -- mirroring the stock C++ model
-- blender (blender_deffer_model.cpp): a deferred G-buffer pass (deffer_model_flat/deffer_base_flat)
-- that marks its pixels as lit scene geometry in the stencil, plus the sun-shadow caster
-- (shadow_direct_model/dumb). Consumes the static model vertex layout (v_model) via SKIN_NONE.
-- Single-sided (default cull), exactly like stock OGF models.

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("deffer_model_flat","deffer_base_flat")
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
	-- Mark these pixels as lit scene geometry in the G-buffer stencil, EXACTLY as the stock
	-- deferred model blender does (blender_deffer_model.cpp):
	--   r_Stencil(TRUE, D3DCMP_ALWAYS, 0xff, 0x7f, KEEP, REPLACE, KEEP); r_StencilRef(0x01);
	-- Without this mark the deferred sky/combine pass treats our pixels as background and
	-- composites the SKYBOX over them wherever nothing else is behind -- so the model is visible
	-- only where a wall/floor happens to sit behind it, and vanishes against the sky.
	shader:dx10stencil	(true, 8, 255, 127, 1, 3, 1)  -- enable, ALWAYS(8), rmask 0xff, wmask 0x7f, KEEP(1), REPLACE(3), KEEP(1)
	shader:dx10stencil_ref	(1)
end

function l_special	(shader, t_base, t_second, t_detail)
	-- Sun shadow-map caster. Matches the stock model shadow pass: z-test + z-WRITE on (this pass
	-- writes the shadow depth -- z-write OFF yields a thin/incomplete cast shadow), colour off.
	shader:begin	("shadow_direct_model",	"dumb")
			: zb		(true,true)
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
	shader:dx10color_write_enable	(false, false, false, false)
end
