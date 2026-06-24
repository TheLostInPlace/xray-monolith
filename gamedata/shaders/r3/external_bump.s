-- external_bump.s : lit + textured + NORMAL-MAPPED shader for external (GLTF/GLB) meshes.
--
-- Same deferred MODEL path as external_static.s, but the G-buffer pass uses the stock model bump
-- vertex shader (deffer_model_bump -> emits the eye-space TBN) paired with a custom pixel shader
-- (deffer_base_ext_bump) that samples a STANDARD glTF tangent-space normal map. Selected by the C++
-- loader (FExternalVisual) only when the glTF material actually has a normal texture; models without
-- one keep using external_static. Textures arrive as a comma list "albedo,normal" -> t_base/t_second.

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("deffer_model_bump","deffer_base_ext_bump")
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)		-- albedo
	shader:dx10texture	("s_bump",	t_second)	-- glTF tangent-space normal map
	shader:dx10sampler	("smp_base")
	-- Mark pixels as lit scene geometry (same as external_static / the stock model blender), else the
	-- sky/combine pass composites the skybox over the model where nothing else is behind it.
	shader:dx10stencil	(true, 8, 255, 127, 1, 3, 1)  -- enable, ALWAYS, rmask 0xff, wmask 0x7f, KEEP, REPLACE, KEEP
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
