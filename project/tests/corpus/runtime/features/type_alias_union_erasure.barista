# A multi-alternative union erases at runtime: values cross union-typed parameters, locals, and
# returns unchanged, with no wrapper, tag, or conversion. A single-alternative alias keeps the
# runtime typing of the type it names, including its numeric width.
type Meters = float
type Small = int
type Anything = int | String | Marker


class Marker:
	var label: String = "marker"


func echo_union(value: Anything) -> Anything:
	return value


func echo_alias(value: Meters) -> Meters:
	return value


func test():
	# Nothing is added to the value on the way in or out.
	var from_int = echo_union(42)
	var from_string = echo_union("text")
	var marker := Marker.new()
	var from_object = echo_union(marker)
	prints(from_int, typeof(from_int) == TYPE_INT)
	prints(from_string, typeof(from_string) == TYPE_STRING)
	prints(from_object == marker, from_object.label)

	# A union-typed local is untyped at runtime, so one slot holds every alternative in turn.
	var slot: Anything = 1
	prints(slot, typeof(slot) == TYPE_INT)
	slot = "reassigned"
	prints(slot, typeof(slot) == TYPE_STRING)

	# A single-alternative alias stays typed: assigning through it converts like the named type.
	var distance: Meters = 3
	prints(distance, typeof(distance) == TYPE_FLOAT)
	prints(echo_alias(1.5), typeof(echo_alias(1.5)) == TYPE_FLOAT)

	var small: Small = 7
	prints(small, typeof(small) == TYPE_INT)
