# A Variant target names no union, so the shorthand is rejected there too.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func test():
	var anything: Variant = .Ok(1)
	print(anything)
