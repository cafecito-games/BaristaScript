# An awaited call lowers through the async path, which keeps the same receiver-before-arguments
# order as a synchronous call.
# https://github.com/cafecito-games/Foundry/issues/2255

var order: Array = []


class Worker:
	async func work(value: int) -> int:
		return value * 2


func note(label: String, value: int) -> int:
	order.append(label)
	return value


func make_worker() -> Worker:
	order.append("receiver")
	return Worker.new()


func test():
	prints("value", await make_worker().work(note("argument", 21)))
	prints("eval", order)
