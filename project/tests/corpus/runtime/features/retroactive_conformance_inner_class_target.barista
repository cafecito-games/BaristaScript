# An inner class of a foreign file is a conformance target in its own right. Its witnesses must bind
# against *its* member layout, not the root class's: the two declare same-named members at different
# offsets, and `Beta` has one the root does not. A target recovered by script path alone yields the
# root class, which both mis-binds the layout and makes the script unserializable to compiled
# bytecode, so this fixture runs in the bytecode round-trip suite too.
extend RtcStaticKits.Alpha uses Pingable:
	func ping() -> int:
		return level


extend RtcStaticKits.Beta uses Pingable:
	func ping() -> int:
		return level + extra


func via_param(p: Pingable) -> int:
	return p.ping()


func test() -> void:
	print(via_param(RtcStaticKits.Alpha.new()))
	print(via_param(RtcStaticKits.Beta.new()))
