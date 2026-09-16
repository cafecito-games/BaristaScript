# The handle layer is compared in the return position too, and both signatures render distinctly.
class Factory extends RefCounted:
	pass


func make_handle() -> Type[Factory]:
	return Factory


func make_instance() -> Factory:
	return Factory.new()


func test() -> void:
	var expects_handle: Callable[[], Type[Factory]] = self.make_instance
	var expects_instance: Callable[[], Factory] = self.make_handle
	print(expects_handle)
	print(expects_instance)
