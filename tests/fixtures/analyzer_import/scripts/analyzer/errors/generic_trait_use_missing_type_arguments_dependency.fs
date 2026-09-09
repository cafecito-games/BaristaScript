# The offending `uses` lives in the helper file, so the arity diagnostic belongs to the helper's
# own analysis. This consumer resolves the helper's class and must report only the established
# dependency-failure message — never a duplicated or order-dependent copy of the helper's error.
func test() -> void:
	var inner := CafecitoBareUseHelper.Inner.new()
	print(inner.stored(1))
