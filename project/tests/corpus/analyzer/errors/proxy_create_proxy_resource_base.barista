# The runtime proxy guard accepts only an exactly-RefCounted base, so a
# Resource-rooted abstract type (Resource derives from RefCounted) is rejected
# statically rather than slipping through to a runtime failure.
abstract class AbstractResource extends Resource:
	abstract func reload() -> void

func handle(_method_name: StringName, _args: Array) -> Variant:
	return null

func test() -> void:
	var bad := create_proxy[AbstractResource](handle)
	print(bad)
