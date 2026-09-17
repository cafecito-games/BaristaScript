trait Cloning:
	abstract func clone_kind() -> String


extend Resource uses Cloning:
	func clone_kind() -> String:
		return Self.new().get_class()


func test() -> void:
	var image := ImageTexture.new()
	print(image.clone_kind())
