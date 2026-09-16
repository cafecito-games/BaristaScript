# `final var` locals inside an inline property accessor are enforced like any
# other function body.
var amount:
	get:
		final var doubled := _value * 2
		doubled = 0
		return doubled
	set(value):
		_value = value

var _value := 0

func test() -> void:
	pass
