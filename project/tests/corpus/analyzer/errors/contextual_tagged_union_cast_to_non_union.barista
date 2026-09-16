# A cast names the type its operand must have, and a type that is not a tagged union names no union
# for the shorthand, so the shorthand is rejected instead of being cast into an unrelated type.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func test():
	var labeled = .Ok(1) as int
	print(labeled)
