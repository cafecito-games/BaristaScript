# The shorthand also heads a match case pattern and the right-hand side of `is`, where the union
# comes from the subject rather than from the source text.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func run(subject: Result[int, String]) -> void:
	match subject:
		.Ok(payload):
			print(payload)
		.Err(reason):
			print(reason)
		_:
			pass

	if subject is .Ok(payload):
		print(payload)
