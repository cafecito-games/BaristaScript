# A surplus argument in a vararg call occupies no declared parameter, so it supplies no union for a
# contextual case shorthand.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func test():
	print(.Ok(1))
