# The exact construction spellings substitute a payload's `Self` field to the declaring class and
# keep ordinary compatibility: a static function of the declaring class has no receiver a `Self`
# leaf could denote, the class-handle spelling names the class it substitutes, and a `final`
# declaring class's single binding admits any value statically typed as the class.
class Receiver:
	enum Message:
		Detach
		Attach(index: int, owner: Self)

	static func construct_static(value: Receiver) -> void:
		var made := Message.Attach(3, value)
		if made is Message.Attach(index, owner):
			prints("static", index, owner == value)


class Sub:
	extends Receiver


final class Solo:
	enum Note:
		Tag(owner: Self)

	func construct_with_value(value: Solo) -> void:
		var made := Note.Tag(value)
		if made is Note.Tag(owner):
			prints("final", owner == value)


func test() -> void:
	var receiver := Receiver.new()
	Receiver.construct_static(receiver)
	var from_handle := Receiver.Message.Attach(4, receiver)
	if from_handle is Receiver.Message.Attach(index, owner):
		prints("handle", index, owner == receiver)
	var sub := Sub.new()
	var sub_case := Receiver.Message.Attach(5, sub)
	if sub_case is Receiver.Message.Attach(index, owner):
		prints("sub", index, owner == sub)
	Solo.new().construct_with_value(Solo.new())
