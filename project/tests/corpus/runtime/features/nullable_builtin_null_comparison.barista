func test():
	var text: String? = null
	print("null String? == null: ", text == null)
	print("null String? != null: ", text != null)

	var number: int? = null
	print("null int? == null: ", number == null)
	print("null int? != null: ", number != null)

	var ratio: float? = null
	print("null float? == null: ", ratio == null)
	print("null float? != null: ", ratio != null)

	var flag: bool? = null
	print("null bool? == null: ", flag == null)
	print("null bool? != null: ", flag != null)

	var offset: Vector2? = null
	print("null Vector2? == null: ", offset == null)
	print("null Vector2? != null: ", offset != null)

	var filled_text: String? = "hi"
	print("filled String? == null: ", filled_text == null)
	print("filled String? != null: ", filled_text != null)

	var filled_number: int? = 5
	print("filled int? == null: ", filled_number == null)
	print("filled int? != null: ", filled_number != null)

	var node: Node? = null
	print("null Node? == null: ", node == null)
	print("null Node? != null: ", node != null)

	var filled_node: Node? = Node.new()
	print("filled Node? == null: ", filled_node == null)
	print("filled Node? != null: ", filled_node != null)
	filled_node.free()

	var other_number: int? = null
	print("null int? == null int?: ", number == other_number)
	print("null int? != null int?: ", number != other_number)
	print("null int? == filled int?: ", number == filled_number)
	print("null int? != filled int?: ", number != filled_number)

	if text != null:
		print("unreachable: null String? passed the null guard")
	else:
		print("null String? correctly failed the null guard")
