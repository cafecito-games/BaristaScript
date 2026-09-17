# A `Self` element in a tuple parameter now reaches run time, so the signature references `Self` and
# the frame resolves it per call: an instance frame against the receiver's leaf script, a static frame
# against the class handle the call was made through. Both routes supply one for a legal call, so the
# hard-fail path for a static frame with no receiver stays unreachable here.
#
# Every call is written through a `Callable`, because the analyzer admits nothing but a value it can
# already prove is the receiver's own type into a `Self` position, and that leaves no direct call with
# an unproven argument to make.
class Node2 extends RefCounted:
	func take(pair: (int, Self)) -> void:
		print("instance took ", pair.0, " ", pair.1 != null)

	static func make(pair: (int, Self)) -> void:
		print("static took ", pair.0, " ", pair.1 != null)


class Derived extends Node2:
	pass


func test() -> void:
	var node := Node2.new()
	var callback: Callable = node.take
	callback.call((1, Node2.new()))

	var static_callback := Callable(Node2, "make")
	static_callback.call((2, Node2.new()))

	# An inherited instance method resolves `Self` against the receiver's leaf, so a `Derived` receiver
	# takes a `Derived` element here and rejects a bare `Node2` one (see the error fixture).
	var derived := Derived.new()
	var derived_callback: Callable = derived.take
	derived_callback.call((3, Derived.new()))
