# A numeric alternative of a `Self`-bearing union admits a value that has to change carrier to reach
# it wherever the store can perform that change -- here an integer literal a `float` alternative holds
# exactly. The rule holds whether or not another alternative names `Self`, and whether or not the
# alternatives that do not name `Self` are numerous enough to still read as a set.
class Receiver:
	func take(value: float | (int, Self)) -> String:
		return str(value)

	func take_plain(value: float | String) -> String:
		return str(value)

	func drive() -> void:
		print("self ", take(5))
		print("plain ", take_plain(5))


func test() -> void:
	Receiver.new().drive()
