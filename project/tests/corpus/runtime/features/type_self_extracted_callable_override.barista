# An extracted static callable keeps both halves of the call it stands for: the class its lookup
# starts from, which decides which implementation runs, and the receiver, which decides what `Self`
# means. A receiver that declares its own function of the same name changes neither for an
# unqualified reference, so extraction and an unqualified direct call agree in every column below.
class Base:
	static func spawn() -> Self:
		return Self.new()

	static func call_direct() -> Variant:
		return spawn()

	static func extract() -> Callable:
		return spawn


class Derived:
	extends Base

	static func spawn() -> Self:
		return Self.new()


class Plain:
	extends Base


func test() -> void:
	print(Derived.call_direct() is Derived, " ", Derived.extract().call() is Derived)
	print(Plain.call_direct() is Plain, " ", Plain.extract().call() is Plain)

	# A qualified extraction off the receiver names the receiver's own function, matching the
	# qualified direct call.
	print(Derived.spawn() is Derived, " ", (Derived.spawn as Callable).call() is Derived)
