# A `Self` position nested through an unnamed tuple element of a payload field is checked by
# identity recursively, so a foreign value in the nested position rejects the whole argument.
class Receiver:
	enum Message:
		Detach
		Deep(index: int, pair: (String, Self))

	func construct_nested(foreign: Receiver) -> void:
		var bad := Message.Deep(2, ("b", foreign))
		print(bad)


func test() -> void:
	pass
