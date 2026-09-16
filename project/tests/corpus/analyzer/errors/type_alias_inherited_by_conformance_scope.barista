# Aliases are file-local, so inheriting the class that declares one does not bring it into the
# inheriting file's scope, not even for a conformance witness written there.
extends TypeAliasConformanceBase

extend FrwWidget uses FrwGadgetlike:
	func frw_gadget() -> String:
		return "gadget:" + str(measure(1.5))

	func measure(distance: Meters) -> Meters:
		return distance * 2.0


func test():
	print(FrwWidget.new().frw_gadget())
