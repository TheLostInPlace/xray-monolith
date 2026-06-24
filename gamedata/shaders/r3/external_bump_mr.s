-- external_bump_mr.s : lit + albedo + NORMAL MAP + METALLIC-ROUGHNESS shader for external models.
--
-- Like external_bump.s but the G-buffer pass also feeds the glTF metallic-roughness map into gloss
-- (deffer_base_ext_bump_mr). Selected by the C++ loader when the material has BOTH a normal map and
-- a metallic-roughness map. All three maps are passed explicitly in the texture list
-- "albedo,normal,metalrough"; the engine forwards the 3rd list texture as a 5th arg (t_metalrough).

function normal		(shader, t_base, t_second, t_detail, t_metalrough, t_ao)
	shader:begin	("deffer_model_bump_vc","deffer_base_ext_bump_mr")
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)			-- albedo
	shader:dx10texture	("s_bump",	t_second)		-- glTF tangent-space normal map
	shader:dx10texture	("s_bumpX",	t_metalrough)	-- glTF metallic-roughness map (G=rough, B=metal)
	shader:dx10texture	("s_ao",	t_ao)			-- glTF occlusion (R); = MR tex for ORM, else separate AO
	shader:dx10sampler	("smp_base")
	shader:dx10stencil	(true, 8, 255, 127, 1, 3, 1)  -- lit-geometry stencil mark (skybox fix)
	shader:dx10stencil_ref	(1)
end

-- Shadow-map caster for ALL light types (sun E[4] / point E[2] / spot E[3]); the engine renders every
-- shadow map from E[SE_R2_SHADOW]=E[2]=l_point, so we fill all three. Matches the stock model shadow
-- (default cull, ztest+zwrite, colour off) but uses shadow_ext_model.vs (normal-offset depth bias) so
-- neither smooth surfaces self-shadow nor thin geometry self-shadows under a close light. See
-- external_static.s for the full why.
function l_point	(shader, t_base, t_second, t_detail, t_metalrough)
	shader:begin	("shadow_ext_model",	"dumb")
			: zb		(true,true)
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
	shader:dx10color_write_enable	(false, false, false, false)
end
l_spot    = l_point
l_special = l_point
