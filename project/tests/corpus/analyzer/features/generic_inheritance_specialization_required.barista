# Negative controls for the rule that a bare generic base is rejected: every other `extends` shape
# stays legal. A non-generic script base, a native base, a concretely specialized generic base, and a
# child forwarding its own parameter all resolve without a diagnostic, and a raw generic *value* of
# the same class is still allowed.
class Box[T]:
	var value: T


class Plain:
	var tag: String = "plain"


# Non-generic script base.
class FromPlain extends Plain:
	func describe() -> String:
		return tag


# Native base.
class FromNative extends RefCounted:
	func describe() -> String:
		return "native"


# Concretely specialized generic base.
class IntBox extends Box[int]:
	func doubled() -> int:
		return value * 2


# Forwarded child parameter.
class Wrapper[U] extends Box[U]:
	func unwrap() -> U:
		return value


func test() -> void:
	print(FromPlain.new().describe())
	print(FromNative.new().describe())

	var ints := IntBox.new()
	ints.value = 21
	print(ints.doubled())

	var wrapped := Wrapper[String].new()
	wrapped.value = "hi"
	print(wrapped.unwrap())

	# A raw generic annotation and value are untouched by the inheritance rule.
	var raw: Box = Box[int].new()
	raw.value = 5
	print(raw.value)

	print("generic inheritance controls ok")
