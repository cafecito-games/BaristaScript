# A contextual case shorthand in a container-literal element takes its union from the container's
# element type, which the literal patcher supplies after the element has been reduced.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


enum Option[T]:
	None
	Some(value: T)


func test():
	var pairs: Array[Result[int, String]] = [.Ok(1), .Err("x")]
	print(pairs)
	print(pairs[0] == Result[int, String].Ok(1))

	# A payload-less case in element position folds to the same singleton the explicit form folds to.
	var options: Array[Option[int]] = [.None, .Some(2)]
	print(options)
	print(options[0] == Option[int].None)

	# A shorthand in payload position resolves against the specialized field type, so a case nested
	# inside a container element resolves too.
	var nested: Array[Result[Result[int, String], String]] = [.Ok(.Ok(1))]
	print(nested)

	# An element of the explicit spelling next to a shorthand keeps its own resolution.
	var mixed: Array[Result[int, String]] = [Result[int, String].Ok(3), .Err("y")]
	print(mixed)
