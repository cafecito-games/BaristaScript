# `abstract` combined with `static` is allowed inside a trait. The using class
# satisfies the requirement with a concrete static method.
extends RefCounted
uses Makeable

trait Makeable:
	abstract static func make() -> String

static func make() -> String:
	return "made"

func test() -> void:
	print(make())
