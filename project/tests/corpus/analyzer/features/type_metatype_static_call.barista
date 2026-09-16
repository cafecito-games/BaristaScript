trait Creatable:
	abstract static func create() -> Self

class User:
	uses Creatable

	static func create() -> User:
		return User.new()

func factory[T: Creatable](factory_type: Type[T]) -> T:
	return factory_type.create()

func concrete_factory(factory_type: Type[User]) -> User:
	return factory_type.create()

func factory_from_callable[T: Creatable](factory_type: Type[T]) -> T:
	var maker := factory_type.create
	return maker.call()

func test():
	var user: User = factory(User)
	var user_type: Type[User] = User
	var concrete_user: User = concrete_factory(user_type)
	var callable_user: User = factory_from_callable(User)
	print(user is User)
	print(concrete_user is User)
	print(callable_user is User)
