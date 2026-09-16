trait Factory:
	abstract static func create() -> Self


class Conforming:
	uses Factory

	static func create() -> Conforming:
		return Conforming.new()


class NonConforming:
	pass


func test():
	var factories: Array[Type[Factory]] = [Conforming, NonConforming]
	print(factories.size())
