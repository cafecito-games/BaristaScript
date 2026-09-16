# A type test refines a set-typed value to the tested alternative for the rest of the branch, so
# operations that only exist on that alternative become available there.
type Scalar = int | String
type MaybeScalar = int? | String


class Holder:
	# A member variable is never narrowed: flow narrowing keys on parameters, locals, iterators and
	# binds only. Copying into a local is the supported way to narrow a member's value.
	var stored: Scalar = "member"

	func length_of_stored() -> int:
		var local := stored
		if local is String:
			return local.length()
		return 0


func describe(value: Scalar) -> String:
	if value is String:
		# `value` is a `String` here, so a `String`-only method resolves.
		return "String(%d)" % value.length()
	return "int"


func describe_explicit_union(value: int | String | float) -> String:
	if value is String:
		return "String(%d)" % value.length()
	if value is float:
		return "float"
	return "int"


func bump[X: Number](value: X) -> int:
	# The value narrows to `int`; the type parameter `X` is untouched by the test.
	if value is int:
		return value + 1
	return 0


func nested(value: int | String | float) -> String:
	if value is not String:
		# `String` is gone, but nothing else has been ruled out yet.
		if value is float:
			return "float"
		return "int"
	else:
		return "String(%d)" % value.length()


func joined(value: Scalar) -> String:
	if value is String:
		print(value.length())
	else:
		print(value + 1)
	# Both branches ended, so `value` is the whole set again and only set-wide operations remain.
	return str(value is String)


func maybe(value: MaybeScalar) -> String:
	if value == null:
		return "null"
	if value is String:
		return "String(%d)" % value.length()
	return "int"


# Numeric alternatives go narrowest-first: `is int` leaves `long` in the surviving set, so the next
# arm is still reachable. The reverse order is rejected; see `type_union_subsumed_type_test_chain`.
func narrowest_first(value: int | long | float) -> String:
	if value is int:
		return "int"
	elif value is long:
		return "long"
	return "float"


# Narrowest-first stays legal even when the set holds nothing but same-carrier integers: `is int`
# leaves `long` behind, so neither test exhausts the alternatives.
func narrowest_first_pair(value: int | long) -> String:
	if value is int:
		return "int"
	return "long"


func test():
	prints(describe("abc"), describe(1))
	prints(describe_explicit_union("ab"), describe_explicit_union(2.5), describe_explicit_union(3))
	prints(bump(4), bump(5.5))
	prints(nested("abcd"), nested(6.5), nested(7))
	prints(joined("ab"), joined(8))
	prints(maybe(null), maybe("abcde"), maybe(9))
	prints(narrowest_first(10), narrowest_first(4294967296L), narrowest_first(11.5))
	prints(narrowest_first_pair(12), narrowest_first_pair(4294967297L))
	print(Holder.new().length_of_stored())
