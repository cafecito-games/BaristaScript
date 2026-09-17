# A tagged union's values are read-only `[tag, payload...]` Arrays. A payload-less case is its own
# `[tag]` singleton and a payload case is built from its declared field types, widening arguments to
# the declared type. Tags are ordinal by declaration order, so the tag is element 0 for every case.
enum RuntimeMessage:
	Quit
	Move(x: int, y: int)
	Write(text: String)
	Scale(factor: float)

func forward(message: RuntimeMessage) -> RuntimeMessage:
	return message

func round_trip(value: Variant) -> Variant:
	return value

func test():
	print(RuntimeMessage.Quit)
	print(RuntimeMessage.Move(1, 2))
	print(RuntimeMessage.Write("hello"))
	# A float field widens an int argument at the construction site.
	print(RuntimeMessage.Scale(2))

	var message: RuntimeMessage = RuntimeMessage.Quit
	print(message)
	message = RuntimeMessage.Move(3, 4)
	print(forward(message))

	# The runtime value escapes into a `Variant` unchanged: still read-only, still tag-first.
	var escaped: Variant = round_trip(RuntimeMessage.Move(5, 6))
	print(escaped)
	@warning_ignore("unsafe_method_access")
	print(escaped.is_read_only())
	print(escaped[0])
	@warning_ignore("unsafe_method_access")
	print(escaped.size())
	@warning_ignore("unsafe_method_access")
	print(round_trip(RuntimeMessage.Quit).is_read_only())
