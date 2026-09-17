# Callable and Signal signatures keep the class-handle layer of a `Type[T]` slot, so a signature
# parameter accepts a class rather than instances of it and the callee receives a usable handle.
trait SignatureFactory extends RefCounted:
	abstract static func describe() -> String


class SignatureWidget extends RefCounted:
	uses SignatureFactory

	static func describe() -> String:
		return "widget"


class SignatureGadget extends RefCounted:
	uses SignatureFactory

	static func describe() -> String:
		return "gadget"


signal registered(name: String, factory: Type[SignatureFactory])


var seen: Array[String] = []


func identity(factory: Type[SignatureFactory]) -> Type[SignatureFactory]:
	return factory


func on_registered(name: String, factory: Type[SignatureFactory]) -> void:
	seen.append(name + ":" + factory.describe())


func collect(factories: Array[Type[SignatureFactory]]) -> void:
	for factory in factories:
		seen.append(factory.describe())


func test():
	var construct: Callable[[Type[SignatureFactory]], String] = func(factory: Type[SignatureFactory]) -> String:
		return factory.describe()
	print(construct.call(SignatureWidget))

	var bound: Callable[[Type[SignatureFactory]], Type[SignatureFactory]] = self.identity
	print(bound.call(SignatureGadget) == SignatureGadget)

	var nested: Callable[[Array[Type[SignatureFactory]]], void] = self.collect
	var factories: Array[Type[SignatureFactory]] = [SignatureWidget, SignatureGadget]
	nested.call(factories)

	registered.connect(on_registered)
	registered.emit("first", SignatureWidget)
	print(seen)
