# A `T?` type test is not a uniform null check at run time: the builtin test opcode accepts null for
# a nullable test, while the native and script opcodes answer false for null whatever the `?` said.
# Failed-test narrowing therefore never removes null; only a null check does. This fixture pins the
# runtime behavior the analyzer relies on.
class Payload:
	var label: String = "payload"


func test():
	var text: String? = null
	prints(text is String, text is String?)

	var payload: Payload? = null
	prints(payload is Payload, payload is Payload?)

	var node: RefCounted? = null
	prints(node is RefCounted, node is RefCounted?)

	var present: Payload? = Payload.new()
	prints(present is Payload, present is Payload?)
