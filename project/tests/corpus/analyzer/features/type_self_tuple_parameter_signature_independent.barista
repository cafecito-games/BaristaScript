# Whether a `Self` parameter is satisfied depends only on the call site, never on the calling
# function's own signature or body. The identical statement compiles in a caller that mentions `Self`
# nowhere, in one whose signature mentions it, and in one whose body mentions it in an unrelated
# declaration.
class Receiver:
	func take_pair(pair: (int, Self)) -> void:
		print("pair ", pair.0)

	func plain_caller() -> void:
		take_pair((1, self))

	func signature_caller() -> Self:
		take_pair((2, self))
		return self

	func unrelated_mention_caller() -> void:
		var _unrelated: Array[Self] = []
		take_pair((3, self))


func test() -> void:
	var receiver := Receiver.new()
	receiver.plain_caller()
	print(receiver.signature_caller() == receiver)
	receiver.unrelated_mention_caller()
