# `:=` infers the target type from the value, so it cannot supply the union the shorthand needs.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func test():
	var inferred := .Ok(1)
	print(inferred)
