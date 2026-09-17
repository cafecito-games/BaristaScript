# `Type[Button]` and `Type[Node]` are distinct element descriptors, so neither container references
# the other. The funnel through `Variant` defeats the static check.
func test():
	var buttons: Array[Type[Button]] = [Button]
	var widened: Variant = buttons
	var nodes: Array[Type[Node]] = widened
	print(nodes.size())
