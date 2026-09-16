# A callable supplied for a fixed-arity `Self` parameter may accept more than the parameter sends: the
# parameter's static type permits exactly its fixed arity, so a tail beyond it is never invoked and its
# element type cannot be observed. Through the calling frame's own receiver the contract admits it and
# the call dispatches; the `Self` in the fixed prefix is still answered by receiver identity, so a
# foreign receiver stays rejected in `analyzer/errors/type_self_callable_tail_mismatch_argument.fs`.
class Cell:
	func invoke(callback: Callable[[Self], void]) -> void:
		callback.call(self)

	func greet(_owner: Self, ...rest: Array) -> void:
		print("greeted ", rest.size())

	func run() -> void:
		var callback: Callable[[Self, ...Array], void] = greet
		invoke(callback)


func test() -> void:
	Cell.new().run()
