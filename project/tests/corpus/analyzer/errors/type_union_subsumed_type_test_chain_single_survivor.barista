# A survivor set that collapses to a single alternative is still a rejection: `is long` removes
# `long` and `int` together, leaving only `float`, and a `float` never passes `is int` because the
# two use different Variant carriers. The diagnostic names the collapsed type rather than a set.
type Scalar = int | long | float


func widest_first(value: Scalar) -> String:
	if value is long:
		return "long"
	elif value is int:
		return "int"
	return "float"


func test():
	print(widest_first(5))
