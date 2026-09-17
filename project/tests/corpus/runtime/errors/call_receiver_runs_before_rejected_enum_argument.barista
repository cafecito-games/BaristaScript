# An enum host-function instance call evaluates its receiver before the argument conversion
# that rejects the call, so the receiver's side effects survive the abort.
# https://github.com/cafecito-games/Foundry/issues/2255
enum Status:
	READY = 1
	DONE = 2

	func accept(_klass: Type[Resource]) -> void:
		print("accepted")


class Holder:
	var value: Status = Status.READY


func make_holder() -> Holder:
	print("receiver ran")
	return Holder.new()


func test():
	var klass: Variant = 1
	make_holder().value.accept(klass)
	print("after call")
