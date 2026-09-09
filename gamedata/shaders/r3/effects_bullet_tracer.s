function normal		(shader, t_base, t_second, t_detail)
	shader	: begin	("stub_default","hud_default")
			: zb	(true,false)
			: blend		(true,blend.srcalpha,blend.one)
--	TODO: DX10: implement aref for this shader
			: aref 		(true,0)
			: dx10color_write_enable( true, true, true, false)

	shader 	: dx10texture	("s_base", t_base)
--	TODO: DX10: Clamp
	shader 	: dx10sampler	("smp_base") --	Clamp
end
