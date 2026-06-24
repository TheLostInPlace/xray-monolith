-- external_static.s : lit + textured shader for external (GLTF/GLB) mesh geometry.
--
-- Renders external meshes through the engine's standard deferred MODEL path so they integrate
-- with scene lighting (sun/point/spot) and cast sun shadows -- mirroring the stock C++ model
-- blender (blender_deffer_model.cpp): a deferred G-buffer pass (deffer_model_flat/deffer_base_flat)
-- that marks its pixels as lit scene geometry in the stencil, plus the sun-shadow caster
-- (shadow_direct_model/dumb). Consumes the static model vertex layout (v_model) via SKIN_NONE.
-- Single-sided (default cull), exactly like stock OGF models.

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("deffer_model_flat","deffer_base_ext_flat")
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

-- Shadow-map caster for ALL light types. The engine renders every shadow map (sun cascade, point, spot)
-- from E[SE_R2_SHADOW] = E[2] = l_point (rimp_select_sh_static/dynamic in r4.cpp); we fill E[2]/E[3]/E[4]
-- (l_point/l_spot/l_special) with the SAME caster so whichever element a phase picks is populated.
-- Matches the stock model shadow EXACTLY (shadow_direct_model/dumb, DEFAULT cull, ztest+zwrite, colour
-- off) but swaps in shadow_ext_model.vs, which adds a normal-offset depth bias -- so neither smooth
-- surfaces self-shadow (acne) nor thin geometry self-shadows under a close light. Our VS only; stock
-- OGF keeps shadow_direct_model, so nothing stock changes.
function l_point	(shader, t_base, t_second, t_detail)
	shader:begin	("shadow_ext_model",	"dumb")
			: zb		(true,true)
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
	shader:dx10color_write_enable	(false, false, false, false)
end
l_spot    = l_point   -- E[3]: spot-light shadow caster (e.g. the headlamp/flashlight)
l_special = l_point   -- E[4]: sun shadow caster
