trait Kinded:
	abstract static func matches(value: Object) -> bool
	abstract static func narrows(value: Object) -> bool


extend Resource uses Kinded:
	static func matches(value: Object) -> bool:
		return value is Self

	static func narrows(value: Object) -> bool:
		return (value as Self) != null


func test() -> void:
	var image := ImageTexture.new()
	var resource := Resource.new()
	print(ImageTexture.matches(image), " ", ImageTexture.matches(resource))
	print(Resource.matches(image), " ", Resource.matches(resource))
	print(ImageTexture.narrows(image), " ", ImageTexture.narrows(resource))
	print(Resource.narrows(image), " ", Resource.narrows(resource))
