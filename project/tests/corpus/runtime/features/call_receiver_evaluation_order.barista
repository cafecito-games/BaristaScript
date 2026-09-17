# `a.f(b)` evaluates `a` before `b`: the receiver is written first, so its side effects run
# first. Covers the ordinary script-method receiver, the native `MethodBind` fast path, and an
# enum host-function instance call.
# https://github.com/cafecito-games/Foundry/issues/2255

var order: Array = []


enum Status:
	READY = 1
	DONE = 2

	func combine(other: Status) -> Status:
		return other


class Box:
	func store(_value: int) -> void:
		pass


class Holder:
	var value: Status = Status.READY


func note_int(label: String, value: int) -> int:
	order.append(label)
	return value


func note_string(label: String, value: String) -> String:
	order.append(label)
	return value


func note_status(label: String, value: Status) -> Status:
	order.append(label)
	return value


func make_box() -> Box:
	order.append("receiver")
	return Box.new()


func make_resource() -> Resource:
	order.append("receiver")
	return Resource.new()


func make_holder() -> Holder:
	order.append("receiver")
	return Holder.new()


func test():
	# Script method on an evaluated receiver.
	order.clear()
	make_box().store(note_int("argument", 1))
	prints("script", order)

	# Native `MethodBind` fast path.
	order.clear()
	make_resource().set_name(note_string("argument", "named"))
	prints("native", order)

	# Enum host-function instance call: the receiver is `make_holder().value`.
	order.clear()
	prints("combined", make_holder().value.combine(note_status("argument", Status.DONE)))
	prints("enum", order)
