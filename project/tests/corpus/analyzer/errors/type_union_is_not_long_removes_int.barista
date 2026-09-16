# `is` on a numeric type checks the Variant carrier and the value's magnitude, never a declared
# width, so every `int` value is also a `long` value. A failed `is long` therefore rules out `int`
# as well: removal is downward-closed, not plain member removal. Only `float` survives here, and the
# member lookup below reports that narrowed type.
type Scalar = int | long | float


func classify(value: Scalar) -> void:
	if value is not long:
		var probe := value.no_such_member
		print(probe)
