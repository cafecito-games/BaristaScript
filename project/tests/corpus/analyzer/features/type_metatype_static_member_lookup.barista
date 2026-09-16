trait Creatable:
	abstract static func create() -> Self


class User:
	uses Creatable

	static var created_count := 0

	static func create() -> User:
		created_count += 1
		return User.new()

	static func describe() -> String:
		return "user-type"


func factory[T: Creatable](factory_type: Type[T]) -> T:
	return factory_type.create()


func concrete_factory(factory_type: Type[User]) -> User:
	return factory_type.create()


func concrete_count(factory_type: Type[User]) -> int:
	return factory_type.created_count


func concrete_describe(factory_type: Type[User]) -> String:
	return factory_type.describe()


func test():
	var user: User = factory(User)
	var concrete_user: User = concrete_factory(User)
	print(user is User)
	print(concrete_user is User)
	print(concrete_count(User))
	print(concrete_describe(User))
