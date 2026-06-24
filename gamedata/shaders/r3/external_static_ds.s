-- external_static_ds.s : DOUBLE-SIDED (no backface cull) variant of external_static.s. Selected by the
-- loader when the glTF material has doubleSided=true. Identical to external_static.s except the G-buffer
-- pass (and the shadow caster) disable backface culling (dx10cullmode 1 = D3DCULL_NONE); the PS flips the
-- shading normal on back faces via SV_IsFrontFace. Our .s/PS only -- stock OGF is unaffected.

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("deffer_model_flat_vc","deffer_base_ext_static")
			: fog		(false)
	shader:dx10cullmode	(1)  -- D3DCULL_NONE (doubleSided)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
	shader:dx10stencil	(true, 8, 255, 127, 1, 3, 1)
	shader:dx10stencil_ref	(1)
end

-- Shadow caster: also no-cull so doubleSided/thin geometry casts shadows from both sides.
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
