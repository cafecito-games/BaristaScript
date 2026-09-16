# The exact construction spellings substitute a named tuple's `Self` field to the declaring class
# and keep ordinary compatibility: a static function of the declaring class has no receiver a `Self`
# leaf could denote, the class-handle spelling names the class it substitutes, and a `final`
# declaring class's single binding admits any value statically typed as the class.
class Receiver:
	tuple Pair(index: int, owner: Self)

	static func construct_static(value: Receiver) -> void:
		var made := Pair(3, value)
		print("static ", made.index, " ", made.owner == value)


class Sub:
	extends Receiver


final class Solo:
	tuple Pair(index: int, owner: Self)

	func construct_with_value(value: Solo) -> void:
		var made := Pair(6, value)
		print("final ", made.index, " ", made.owner == value)


func test() -> void:
	var receiver := Receiver.new()
	Receiver.construct_static(receiver)
	var from_handle := Receiver.Pair(4, receiver)
	print("handle ", from_handle.index, " ", from_handle.owner == receiver)
	var sub := Sub.new()
	var sub_pair := Receiver.Pair(5, sub)
	print("sub ", sub_pair.index, " ", sub_pair.owner == sub)
	Solo.new().construct_with_value(Solo.new())
