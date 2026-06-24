-- external_mr_ds.s : DOUBLE-SIDED variant of external_mr.s (no backface cull). Selected when the glTF
-- material has doubleSided=true. Same bindings as external_mr.s; the G-buffer pass + shadow caster set
-- dx10cullmode 1 (D3DCULL_NONE) and the PS flips the normal on back faces via SV_IsFrontFace.

function normal		(shader, t_base, t_second, t_detail, t_ao)
	shader:begin	("deffer_model_flat_vc","deffer_base_ext_mr")
			: fog		(false)
	shader:dx10cullmode	(1)  -- D3DCULL_NONE (doubleSided)
	shader:dx10texture	("s_base",	t_base)			-- albedo
	shader:dx10texture	("s_bump",	t_second)		-- glTF metallic-roughness map (G=rough, B=metal)
	shader:dx10texture	("s_ao",	t_ao)			-- glTF occlusion (R)
	shader:dx10sampler	("smp_base")
	shader:dx10stencil	(true, 8, 255, 127, 1, 3, 1)
	shader:dx10stencil_ref	(1)
end

function l_point	(shader, t_base, t_second, t_detail)
	shader:begin	("shadow_ext_model",	"dumb")
			: zb		(true,true)
			: fog		(false)
	shader:dx10cullmode	(1)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
	shader:dx10color_write_enable	(false, false, false, false)
end
l_spot    = l_point
l_special = l_point
