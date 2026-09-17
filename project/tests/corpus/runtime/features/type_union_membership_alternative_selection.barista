# Which alternative claims a value more than one of them could hold.
#
# The rule is two-tiered: the first alternative in canonical order the value ALREADY satisfies wins,
# and only if none does is the first that admits it BY RETYPING chosen. An exact match is preferred
# over a converting one because retyping copies -- binding a value to a type it already has is cheaper
# and less surprising than rewriting its representation to reach an alternative that merely sorts
# earlier. It is the same preference the non-union parameter binding applies.
#
# Both spellings below normalize to the same canonical order, so the answer does not depend on how the
# union was written -- only on what the value is.
func exact_first(value: Array[int] | (int, int)) -> Variant:
	return value


func exact_first_reversed(value: (int, int) | Array[int]) -> Variant:
	return value


# No alternative admits `[1, 2]` as it stands -- a `(String, String)` tuple has the right arity but the
# wrong elements -- so the retyping pass runs and `Array[int]` takes it.
func retype_when_no_exact(value: Array[int] | (String, String)) -> Variant:
	return value


func describe(slot: Variant) -> String:
	# A tuple alternative stores the canonical read-only, untyped Array; a retyped container alternative
	# stores a typed, still-mutable one. The two are distinguishable without naming either.
	return "read_only=" + str(slot.is_read_only()) + " typed=" + str(slot.is_typed())


func test() -> void:
	var carried: Variant = [1, 2]

	# The tuple is canonically second, and still wins: the value already is a `(int, int)`.
	print("exact first ", describe(exact_first(carried)))
	print("exact first reversed ", describe(exact_first_reversed(carried)))

	# Nothing matches exactly, so the earliest retypable alternative takes it.
	print("retype fallback ", describe(retype_when_no_exact(carried)))
