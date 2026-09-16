# A fixed-arity bind() must not narrow the resulting callable's arity range further than the
# semantics require. Binding K values against a base with D trailing default parameters still
# lets the base's own defaults cover their D parameters independently of the bound values, so the
# minimum call arity of the bound result is `n - D - K`, not `n - D`. Concretely, binding one
# value to the last of three parameters (the middle of which has no default) still allows the
# resulting callable to be invoked with only the first parameter supplied, dispatching the bound
# value into the middle parameter and letting the trailing default fill the last one.
extends RefCounted


func add_with_default(p: int, q: int, s: int = 1) -> int:
	return p + q + s


func test() -> void:
	var bound := Callable(self, "add_with_default").bind(5)
	# The base has one default parameter (s) and one bound value (5). The result must still accept
	# a single-argument call, dispatching add_with_default(2, 5) with s defaulted to 1.
	print(bound.call(2))
