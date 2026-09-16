# Callable.bind()/bindv()/unbind() are variadic builtins. On a typed callable whose target
# signature is known, the analyzer must not reject bound arguments that exceed the target's
# arity: bind()/bindv() accept any number of arguments. Leading bound arguments that line up
# with known parameters are still type-checked, the resulting callable's signature is narrowed
# accordingly, and the async marker is preserved through bind()/unbind().
extends RefCounted


func synchronous() -> int:
	return 1


func one(value: int) -> int:
	return value


async func async_two(a: int, b: int) -> String:
	return str(a) + str(b)


func test() -> void:
	# bind() is variadic: binding an argument to a zero-arity target is accepted.
	var sync_callable := Callable(self, "synchronous")
	print(sync_callable.bind(1).is_async())

	# A correct bind narrows the signature; binding the only parameter yields a 0-arg callable.
	var single := Callable(self, "one")
	print(single.bind(7).call())

	# bindv() is likewise variadic over the array contents.
	print(single.bindv([1, 2]).is_async())

	# unbind() widens the signature past the target's arity without an error.
	print(single.unbind(3).is_async())

	# AsyncCallable keeps its async marker through bind() and unbind().
	var handler: AsyncCallable[[int, int], String] = async_two
	var bound_async := handler.bind(4)
	print(bound_async.is_async())
	print(await bound_async.call(3))
	print(handler.unbind(2).is_async())
