# Argument validation applies to qualified usages just like short-name usages.
namespace cafecito.qualified_args

annotation timeout(seconds: float) targets METHOD

@cafecito.qualified_args.timeout("nope")
func test() -> void:
	pass
