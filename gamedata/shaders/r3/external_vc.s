-- external_vc.s : vertex-coloured (glTF COLOR_0) shader for external (GLTF/GLB) models that have no
-- base-colour texture -- the per-vertex colour IS the surface colour (e.g. BoxVertexColors). Uses a
-- custom VS (deffer_model_flat_vc) that passes COLOR0 through, and a PS that reads it as the albedo.
-- s_base is still bound for the shadow caster (depth only). Same deferred/stencil/shadow setup as
-- external_static.

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("deffer_model_flat_vc","deffer_base_ext_vc")
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
	shader:dx10stencil	(true, 8, 255, 127, 1, 3, 1)  -- lit-geometry stencil mark (skybox fix)
	shader:dx10stencil_ref	(1)
end

-- Shadow-map caster for ALL light types (sun E[4] / point E[2] / spot E[3]); the engine renders every
-- shadow map from E[SE_R2_SHADOW]=E[2]=l_point, so we fill all three. Matches the stock model shadow
-- (default cull, ztest+zwrite, colour off) but uses shadow_ext_model.vs (normal-offset depth bias) so
-- neither smooth surfaces self-shadow nor thin geometry self-shadows under a close light. See
-- external_static.s for the full why.
function l_point	(shader, t_base, t_second, t_detail)
	shader:begin	("shadow_ext_model",	"dumb")
			: zb		(true,true)
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
	shader:dx10color_write_enable	(false, false, false, false)
end
l_spot    = l_point
l_special = l_point
