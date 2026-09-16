# The first bind() below produces a non-contiguous "extra allowed argument count": binding an int
# against the base's trailing default fails at the String default one shift over, but succeeds one
# shift further at the earlier int parameter, so that further-out arity survives as a gap rather
# than a contiguous default count. A second, chained bind() must preserve that gap under the same
# "only a proven mismatch drops it" rule the shift check itself uses: a gradually typed (Variant)
# value bound on the second call must not narrow it away just because its static type cannot prove
# the fit.
extends RefCounted


func combine(a: int, b: int, c: String = "x", d: int = 4) -> String:
	return str(a) + str(b) + c + str(d)


func test() -> void:
	var once_bound := Callable(self, "combine").bind(10)
	var gradual_value: Variant = 20
	var twice_bound := once_bound.bind(gradual_value)
	print(twice_bound.call())
