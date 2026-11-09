-- stamps the lens view depth into the position target and the depth buffer where the lens is visible
function normal(shader, t_base, t_second, t_detail)
	shader:begin("scope_vertex", "scope_lens_depth")
	: zb(true, true)
	: scopelense(2)
end
