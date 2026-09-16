# A generic union spelled without its type arguments is rejected as an annotation, so the declaration
# supplies no expected type at all and the shorthand reports the annotation suggestion alongside the
# arity error rather than naming the bare union.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func test():
	var x: Result = .Ok(1)
	print(x)
