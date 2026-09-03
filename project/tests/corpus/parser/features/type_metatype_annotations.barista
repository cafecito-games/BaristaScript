trait Factory:
	abstract static func create() -> Self


class User:
	uses Factory

	static func create() -> User:
		return User.new()


class Box[T]:
	var value: T


signal selected(klass: Type[Node])

var user_type: Type[User] = User
var node_type: Type[Node] = Button
var factory_type: Type[Factory] = User
var int_box_type: Type[Box[int]] = Box[int]


func accept[T](klass: Type[T]) -> Type[T]:
	return klass


func build[T: Factory](klass: Type[T]) -> T:
	return klass.create()


func return_user_type() -> Type[User]:
	return User


func test():
	var local_type: Type[User] = accept(User)
	var built: User = build(User)
	selected.emit(node_type)
	print(user_type == User)
	print(node_type == Button)
	print(factory_type == User)
	print(int_box_type == Box[int])
	print(local_type == return_user_type())
	print(built is User)
