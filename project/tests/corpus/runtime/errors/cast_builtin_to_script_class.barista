# A builtin value can only reach a script-typed cast target through a retroactive builtin conformance,
# which a plain class target never has. Such a cast is a mistake rather than a value that happens not to
# belong, so it is reported instead of yielding null.
class Plain:
	var value: int = 0


func test():
	var integer: Variant = 1
	@warning_ignore("unsafe_cast")
	print(integer as Plain)
