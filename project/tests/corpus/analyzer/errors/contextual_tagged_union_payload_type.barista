# Payload types are checked against the applied specialization, not the declaration's parameters.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func test():
	var wrong: Result[int, String] = .Ok("one")
	print(wrong)
