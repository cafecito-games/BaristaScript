# A freed instance is reported by `is` and by `as` before either asks about membership, so the
# operand is never answered as a plain mismatch: `false` and `null` are the answers for a live value
# that does not belong, and a dangling one has to be told apart from them.
class Trinket extends Node:
	var value: int = 0


func subtest_is():
	var freed: Variant = Trinket.new()
	freed.free()
	print(freed is Trinket)


func subtest_as():
	var freed: Variant = Trinket.new()
	freed.free()
	print(freed as Trinket)


func test():
	subtest_is()
	subtest_as()
