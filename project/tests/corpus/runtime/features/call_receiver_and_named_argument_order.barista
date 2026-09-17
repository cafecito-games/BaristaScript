# Receiver-first composes with the named-argument rule: the receiver runs first, then the
# argument expressions in source (written) order regardless of parameter order.
# https://github.com/cafecito-games/Foundry/issues/2255

var order: Array = []


class Sink:
	func take(a: int, b: int, c: int) -> void:
		prints(a, b, c)


func note(label: String, value: int) -> int:
	order.append(label)
	return value


func make_sink() -> Sink:
	order.append("receiver")
	return Sink.new()


func test():
	make_sink().take(c = note("c", 3), a = note("a", 1), b = note("b", 2))
	prints("eval", order)
