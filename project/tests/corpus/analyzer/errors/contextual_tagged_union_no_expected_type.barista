# Without a declared target type there is no union to qualify the shorthand with, so the shorthand
# is rejected rather than guessing a union.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func test():
	var inferred = .Ok(1)
	print(inferred)
