# A tagged union declared in an inner class body resolves its self-referential payload types
# through the same member path an outer declaration uses.
class Document:
	enum Value:
		Number(amount: int)
		List(items: Array[Value])
		Object(fields: Dictionary[String, Value])
		Alias(target: Value)

	static func sum(node: Value) -> int:
		match node:
			Value.Number(var amount):
				return amount
			Value.List(var items):
				var total := 0
				for item: Value in items:
					total += sum(item)
				return total
			Value.Object(var fields):
				var total := 0
				for key: String in fields:
					total += sum(fields[key])
				return total
			Value.Alias(var target):
				return sum(target)
		return 0

func test():
	var nested := Document.Value.Object({
		"numbers": Document.Value.List([Document.Value.Number(1), Document.Value.Number(2)]),
		"alias": Document.Value.Alias(Document.Value.Number(4)),
	})
	print(Document.sum(nested))
