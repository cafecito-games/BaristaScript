# A numeric alternative that names no `Self` admits a carrier-crossing constant exactly as it would
# standing in a union of its own: the value's exact magnitude is known to fit the alternative's range,
# so the literal is baked onto that alternative's carrier. The rule holds whether or not another
# alternative names `Self`.
class Receiver:
	func take_self(value: uint | (int, Self)) -> String:
		return str(value)

	func take_plain(value: uint | String) -> String:
		return str(value)

	func drive() -> void:
		print("self ", take_self(5))
		print("plain ", take_plain(5))


func test() -> void:
	Receiver.new().drive()
