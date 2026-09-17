# The analyzer rejects element assignment on a hard tuple type. The read-only Array is the runtime
# backstop for a tuple that escaped into a `Variant`, where no static shape is known.
func subtest_element_assignment(escaped: Variant):
	escaped[0] = 99

func subtest_append(escaped: Variant):
	escaped.append(3)

func subtest_nested_element_assignment(escaped: Variant):
	escaped[0][0] = 99

func test():
	var escaped: Variant = (1, 2)
	subtest_element_assignment(escaped)
	subtest_append(escaped)
	print(escaped)

	# A nested tuple is built by the same construction opcode, so it is read-only in its own right.
	var nested: Variant = ((1, 2), 3)
	subtest_nested_element_assignment(nested)
	print(nested)
