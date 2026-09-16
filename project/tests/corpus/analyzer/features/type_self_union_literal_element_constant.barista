# A constant is admitted by the value it holds, not only by the type it was written as, so an
# alternative whose element the constant fits is an alternative the literal could have been written
# for. That makes exactly one alternative reachable here, the literal is built against it, and its
# elements are converted -- the same answer the alternative gives standing alone.
class Receiver:
	func take_union(entries: Array[(uint, Self)] | Array[(String, Self)]) -> String:
		return str(entries)

	func take_single(entries: Array[(uint, Self)]) -> String:
		return str(entries.size())

	func drive() -> void:
		print("union ", take_union([(5, self)]) != "")
		print("single ", take_single([(5, self)]))


func test() -> void:
	Receiver.new().drive()
