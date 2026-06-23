-- external_static.s : lit + textured shader for external (GLTF/GLB) mesh geometry.
--
-- Renders external meshes through the engine's standard deferred MODEL path so they integrate
-- with scene lighting (sun/point/spot) and cast sun shadows -- the same passes the C++ model
-- blender (CBlender_Model_EbB) uses: a deferred G-buffer pass (deffer_model_flat/deffer_base_flat)
-- and the sun-shadow caster (shadow_direct_model/dumb). NOT emissive (that was the Phase-1
-- fullbright debug shader). Consumes the static model vertex layout (v_model) via SKIN_NONE.
-- The deferred lighting passes (engine-internal) light whatever this writes to the G-buffer;
-- l_point/l_spot elements are R1-forward only and not needed here.

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("deffer_model_flat","deffer_base_flat")
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
	shader:dx10stencil	( 	true, cmp_func.always,
							255 , 127,
							stencil_op.keep, stencil_op.replace, stencil_op.keep)
	shader:dx10stencil_ref	(1)
end

function l_special	(shader, t_base, t_second, t_detail)
	shader:begin	("shadow_direct_model",	"dumb")
			: zb 		(true,false)
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
end
