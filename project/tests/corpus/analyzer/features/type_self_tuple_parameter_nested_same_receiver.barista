# The identity rule is positional and recursive, so a `Self` nested two tuples deep is satisfied the
# same way one nested a single level is, every `Self` position of a multi-`Self` tuple is checked on
# its own, and a nullable tuple parameter unwraps its nullability before the shape is matched -- so it
# admits both the identity literal and `null`.
class Receiver:
	func take_nested(pair: (int, (String, Self))) -> void:
		print("nested ", pair.0)

	func take_both(pair: (Self, Self)) -> void:
		print("both ", pair.0 == pair.1)

	func take_optional(pair: (int, Self)?) -> void:
		if pair == null:
			print("optional none")
		else:
			print("optional ", pair.0, " same=", pair.1 == self)


func test() -> void:
	var receiver := Receiver.new()
	receiver.take_nested((1, ("a", receiver)))
	receiver.take_both((receiver, receiver))
	receiver.take_optional((2, receiver))
	receiver.take_optional(null)
