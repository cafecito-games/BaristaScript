# A typed carrier alternative keeps the carrier rule inside a union: an `Array[Self]` built outside
# the receiver's frame is rejected for an open receiver just as a bare `Array[Self]` parameter is.
class Receiver:
	func absorb(_entries: int | Array[Self]) -> void:
		pass


func test() -> void:
	var receiver := Receiver.new()
	var entries: Array[Receiver] = [Receiver.new()]
	receiver.absorb(entries)
