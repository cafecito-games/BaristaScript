# create_proxy[T] mirrors the runtime guard: the proxy host is a RefCounted, so a
# trait/abstract type rooted on a different native base (here Node) is rejected
# statically instead of only failing at runtime.
abstract class AbstractNode extends Node:
	abstract func ping() -> void

func handle(_method_name: StringName, _args: Array) -> Variant:
	return null

func test() -> void:
	var bad := create_proxy[AbstractNode](handle)
	print(bad)
