# An extracted static callable is the pair of the selected function and the exact receiver it was
# extracted from, so invoking it later resolves `Self` to whatever a direct call through the same
# handle would have. Each line prints the extracted result next to the direct one.
class Base:
	static func spawn() -> Self:
		return Self.new()

	static func extract() -> Callable:
		return spawn


class Derived:
	extends Base


func hold() -> Array[Callable]:
	var handles: Array[Callable] = []
	handles.append(Derived.spawn)
	handles.append(Base.spawn)
	return handles


func test() -> void:
	var derived_callable: Callable = Derived.spawn
	print(derived_callable.call() is Derived, " ", Derived.spawn() is Derived)

	var base_callable: Callable = Base.spawn
	print(base_callable.call() is Base, " ", base_callable.call() is Derived)

	# Two callables for the same inherited implementation, extracted from different receivers. Each
	# keeps its own specialization no matter which one ran first.
	print(base_callable.call() is Derived, " ", derived_callable.call() is Derived)

	# Extraction from inside the inherited frame follows the receiver the call began on, exactly as an
	# unqualified direct call does.
	print(Derived.extract().call() is Derived, " ", Base.extract().call() is Derived)

	# A callable that outlives the scope it was extracted in still carries its receiver.
	var escaped := hold()
	print(escaped[0].call() is Derived, " ", escaped[1].call() is Derived)
