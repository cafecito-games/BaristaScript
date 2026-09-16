# A `match` branch is only reached once every earlier pattern failed, but that ordering is not
# modeled as narrowing: each pattern still sees the whole set. A pattern that subsumes every
# alternative is therefore not the always-true test a condition in the same shape would be, so
# patterns are exempt from that rule and this ordering analyzes cleanly.
type Small = int | long


func narrowest_first(value: Small) -> String:
	match value:
		value is int:
			return "int"
		value is long:
			return "long"
		_:
			return "other"


func test():
	pass
