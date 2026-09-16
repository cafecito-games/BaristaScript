# A fully qualified usage can reference a declaration in the same file by its canonical identity.
namespace cafecito.qualified_local

annotation marker targets METHOD
annotation tag(value: String) targets METHOD

@cafecito.qualified_local.marker
@cafecito.qualified_local.tag("smoke")
func test() -> void:
	pass
