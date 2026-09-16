# A `final` non-generic implementer admits exactly one receiver identity, so a `Self` it writes in a
# trait argument denotes that class at every nesting depth: bare, inside a specialized argument,
# inside typed containers and tuples, under a nullable layer, and behind `Type[...]`. A non-final
# implementer, a subclass of one, and a `final` generic implementer all keep `Self` open, so their
# stores stay accepted on absent evidence rather than on a match.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


trait Storing[T]:
	uses Keeper[T]


class Pair[A, B]:
	pass


final class Closed:
	uses Keeper[Pair[int, Self]]


final class ArrayClosed:
	uses Keeper[Array[Self]]


final class DictionaryClosed:
	uses Keeper[Dictionary[String, Self]]


final class TupleClosed:
	uses Keeper[(int, Self)]


final class NullableClosed:
	uses Keeper[Self?]


final class HandleClosed:
	uses Keeper[Type[Self]]


final class SupertraitClosed:
	uses Storing[Pair[int, Self]]


class OpenSelf:
	uses Keeper[Pair[int, Self]]


class OpenSelfChild extends OpenSelf:
	pass


final class FinalBox[V]:
	uses Keeper[Pair[int, Self]]


func test() -> void:
	var closed: Keeper[Pair[int, Closed]] = Closed.new()
	print(closed.label())

	var array_closed: Keeper[Array[ArrayClosed]] = ArrayClosed.new()
	print(array_closed.label())

	var dictionary_closed: Keeper[Dictionary[String, DictionaryClosed]] = DictionaryClosed.new()
	print(dictionary_closed.label())

	var tuple_closed: Keeper[(int, TupleClosed)] = TupleClosed.new()
	print(tuple_closed.label())

	var nullable_closed: Keeper[NullableClosed?] = NullableClosed.new()
	print(nullable_closed.label())

	var handle_closed: Keeper[Type[HandleClosed]] = HandleClosed.new()
	print(handle_closed.label())

	var supertrait_closed: Keeper[Pair[int, SupertraitClosed]] = SupertraitClosed.new()
	print(supertrait_closed.label())

	# `Self` stayed open on each of these, so the destination is accepted for want of evidence rather
	# than because the implementer proved it.
	var open_self: Keeper[Pair[int, String]] = OpenSelf.new()
	print(open_self.label())

	var open_child: Keeper[Pair[int, String]] = OpenSelfChild.new()
	print(open_child.label())

	var final_generic: Keeper[Pair[int, String]] = FinalBox[int].new()
	print(final_generic.label())
