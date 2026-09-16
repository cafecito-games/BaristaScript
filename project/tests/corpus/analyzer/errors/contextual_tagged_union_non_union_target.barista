# A target that is not a tagged union cannot qualify the shorthand, including a plain int enum whose
# cases are ordinary integer constants rather than union values.
enum Severity:
	LOW = 0
	HIGH = 1


func test():
	var not_a_union: Severity = .LOW
	var not_even_an_enum: int = .Ok(1)
	print(not_a_union, not_even_an_enum)
