# Tuple construction is decided before ordinary call resolution, so the declaration-site fallback has
# to yield to the target's *whole* surface: `label` names an inherited method, `stamp` a method
# flattened in from an applied trait, and `get_class` a method of the native base. All three stay
# calls even though this file declares same-named tuples, while an uncollided declaring-file tuple is
# still constructible from the witness.
extend RtcScopeInheritedLeaf uses RtcScopeLabelling:
	func described() -> String:
		var span: Span = Span(1, 2)
		return label("a") + "/" + stamp() + "/" + get_class() + "/" + str(span.high)


trait RtcScopeLabelling:
	abstract func described() -> String


tuple label(first: int, second: int)
tuple stamp(first: int, second: int)
tuple get_class(first: int, second: int)
tuple Span(low: int, high: int)


func test() -> void:
	var described: RtcScopeLabelling = RtcScopeInheritedLeaf.new()
	print(described.described())
