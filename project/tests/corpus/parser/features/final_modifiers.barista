# `final` parses as a leading modifier on member variables, static variables,
# methods, inner classes, and local variables. It also stacks with `static`
# (order: `final static`). Semantic enforcement lands in later issues; here the
# script must parse, analyze, and run cleanly.

final var member_value := 10
final static var shared_count := 0

final func compute() -> int:
	return member_value

final static func make() -> int:
	return 42

final class Inner:
	final var inner_value := 5

	final func describe() -> String:
		return "inner"

func test():
	final var local_value := compute()
	print(local_value)
	print(shared_count)
	print(make())
	var inner := Inner.new()
	print(inner.inner_value)
	print(inner.describe())
