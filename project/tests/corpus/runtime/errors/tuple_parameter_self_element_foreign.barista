# `Self` resolves against the receiver's leaf script, so a bare `Node2` does not satisfy the `Self`
# element of a method reached through a `Derived` receiver. The matching call goes first, proving the
# parameter is otherwise callable.
class Node2 extends RefCounted:
	func take(pair: (int, Self)) -> void:
		print("took ", pair.0)


class Derived extends Node2:
	pass


func test() -> void:
	var derived := Derived.new()
	var callback: Callable = derived.take
	callback.call((1, Derived.new()))
	callback.call((2, Node2.new()))
	print("unreachable")
