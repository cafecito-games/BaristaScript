# A union erases to one untyped slot, so the runtime check that normally licenses a downcast sees only
# the stored value. A declared integer width is not part of that value, so an alternative wider than
# the destination cannot be verified at run time and is rejected statically, exactly like the concrete
# assignment it stands for.
type Scalar = int | long
type MaybeScalar = int? | long
# A single-member alias collapses to the member and keeps its declared width.
type Wide = long


func take_int(value: int) -> int:
	return value


func test():
	var wide: Scalar = 2147483648L
	var narrow: int = wide
	var concrete: long = 2147483648L
	var also_narrow: int = concrete
	var collapsed: Wide = 2147483648L
	var from_alias: int = collapsed
	var maybe: MaybeScalar = 7
	var from_nullable: int = maybe
	prints(narrow, also_narrow, from_alias, from_nullable, take_int(wide))
