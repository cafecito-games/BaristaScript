# A tuple slot holds a canonical tuple carrier -- an untyped, read-only Array -- for the slot's whole
# lifetime, not merely at the instant of the store. The structural test is unchanged: a shape
# compatible *mutable* Array still satisfies a tuple type, exactly as `tuple_type_test_erasure.fs`
# pins. What changed is that the store normalizes the accepted value into the carrier every tuple has
# instead of retaining the caller's Array, so a write through a retained alias can no longer land a
# value in the slot that its declared shape forbids.
#
# The normalization builds a fresh carrier and never freezes the source: the caller's own Array stays
# mutable and its own writes keep succeeding. It is also shallow in exactly the way
# `OPCODE_CONSTRUCT_TUPLE`'s freeze is shallow -- it walks the tuple spine only, so an element that is
# a shared container keeps its mutability, its identity, and its element typing.

func supply(value: Variant) -> Variant:
	return value


# A tuple's runtime carrier is a read-only, *untyped* Array. Reaching it takes a deliberate unsafe
# cast, since a tuple type is not an Array type to the analyzer.
func report_carrier(value: Variant) -> void:
	var carrier := value as Array
	print(carrier.is_read_only())
	print(carrier.is_typed())


func test() -> void:
	# The laundering the store used to permit. The mutable Array still passes the `is` test, the store
	# still accepts it, and the caller's later write through its retained alias still succeeds -- but
	# it no longer reaches the slot, so an `int` element cannot end up holding a String.
	var mutable: Variant = supply([1, "one"])
	print(mutable is (int, String))
	var slot: (int, String) = mutable
	var alias: Array = mutable as Array
	alias[0] = "laundered"
	print(slot)
	print(alias)
	print(alias.is_read_only())
	report_carrier(slot)

	# A mutable *typed* Array is structurally a `(int, int)`, and the carrier the slot receives is
	# untyped, so exactly one runtime representation stands for a tuple type.
	var typed_source: Array[int] = [10, 20]
	var from_typed: (int, int) = supply(typed_source)
	print(from_typed)
	report_carrier(from_typed)

	# The walk is recursive over the tuple spine. The inner Array is a tuple by declaration, so it is
	# canonicalized too, and mutating the source's inner Array afterwards does not reach the slot.
	var inner: Array = ["four", true]
	var nested: (int, (String, bool)) = supply([4, inner])
	inner[0] = "mutated"
	print(nested)
	print(inner)
	report_carrier(nested)
	report_carrier(nested.1)

	# The spine only. An element declared as a shared container keeps its own mutability and identity,
	# the same deliberate shallow rule `OPCODE_CONSTRUCT_TUPLE` documents, so a later write through the
	# caller's reference is observable through the slot. Its element typing is preserved as well.
	var shared: Array[int] = [1, 2]
	var with_shared: (int, Array[int]) = supply([9, shared])
	shared[0] = 99
	print(with_shared)
	print((with_shared.1).is_typed())

	# A value that was already canonical is stored as itself: still equal to an equal tuple, still
	# hashable as a Dictionary key, still a read-only untyped carrier.
	var from_tuple: (int, String) = supply((5, "five"))
	print(from_tuple == (5, "five"))
	var by_key := {}
	by_key[from_tuple] = "stored"
	print(by_key[(5, "five")])
	report_carrier(from_tuple)

	# A nullable tuple slot still accepts null: a non-Array value is returned unchanged.
	var maybe: (int, String)? = supply(null)
	print(maybe == null)
