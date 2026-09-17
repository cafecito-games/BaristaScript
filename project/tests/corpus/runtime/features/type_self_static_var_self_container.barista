# `resolve_datatype` lowers `Self` to the `@Self` type parameter in every class-body type
# position, including `static var`. A per-specialization element type is not legal on a static slot
# (one storage cell is shared by every specialization), so the declaration is rejected -- and the
# `@Self` in the diagnostic proves the lowering ran on the static variable's type, not only on
# instance member variables.
class Base:
	static var pool: Array[Self] = []
