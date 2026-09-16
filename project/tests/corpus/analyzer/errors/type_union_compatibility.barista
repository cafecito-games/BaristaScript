# A concrete source needs one alternative that accepts it; a union source needs every alternative
# to satisfy the concrete target, because nothing narrows the value at the boundary.
class Left:
	var label: String = "left"


class Right:
	var label: String = "right"


type Either = Left | Right
type Scalar = int | uint


func take_string(value: String) -> String:
	return value


func test():
	var wrong: Scalar = "text"
	var either: Either = Left.new()
	var text: String = either
	var scalar: Scalar = 1
	# Reaching "float" would need the value converted, but a union is one untyped slot at runtime and
	# no per-alternative conversion can be emitted for it.
	var as_float: float = scalar
	prints(wrong, text, as_float, take_string(scalar))
