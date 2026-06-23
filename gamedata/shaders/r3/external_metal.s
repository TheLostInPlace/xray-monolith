-- external_metal.s : forward additive COLORED-REFLECTION overlay for external (GLTF/GLB) models.
--
-- Rendered as a second batch over a metallic material, IN ADDITION to its lit deferred batch
-- (external_mr / external_bump_mr). Same forward/additive/strict-sorted setup as external_emissive
-- (:sorting(2,true) -> the 'Sorted' list, drawn by render_forward() after the deferred combine), so it
-- ADDS a sky-tinted reflection on top of the already-lit pixels and never touches the G-buffer or any
-- shared lighting shader. This is how glTF metalness is done stock-safely: the metal flag can't be
-- packed into the FP16 G-buffer for the shared lighting to read, so the metal response lives entirely
-- in our own pass. See deffer_base_ext_metal.ps.
--
-- s_env0/s_env1 are the engine's day/night sky cubemaps ($user$sky0/1), the same ones hmodel reflects.

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("deffer_model_flat","deffer_base_ext_metal")
			: sorting	(2, true)							-- forward 'Sorted' list, post-combine
			: blend		(true, blend.one, blend.one)		-- additive: scene += reflection
			: zb		(true, false)						-- test against scene depth, don't write
			: dx10zfunc	(cmp_func.lessequal)				-- overlay shares the lit mesh's exact depth
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)					-- albedo (metal reflection colour)
	shader:dx10texture	("s_bumpX",	t_second)				-- glTF metallic-roughness map (B = metal)
	shader:dx10texture	("s_env0",	"$user$sky0")			-- current sky cube
	shader:dx10texture	("s_env1",	"$user$sky1")			-- target  sky cube
	shader:dx10sampler	("smp_base")
	shader:dx10sampler	("smp_rtlinear")
end
