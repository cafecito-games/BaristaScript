# A variable whose type was inferred from its initializer is still dynamic, so the assignment expects
# nothing of the shorthand and the message keeps the suggestion to annotate the target rather than
# naming a type the position does not actually require.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func test():
	var x = 5
	x = .Ok(1)
	print(x)
