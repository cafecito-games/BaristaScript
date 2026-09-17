trait Factory extends RefCounted:
	abstract static func create() -> Self


class User extends RefCounted:
	uses Factory

	static func create() -> User:
		return User.new()


var types: Dictionary[String, Type[Factory]] = {
	"user": User,
}


func build(name: String) -> Factory:
	var factory: Type[Factory] = types[name]
	return factory.create()


func test():
	var built = build("user")
	print(built is User)
	print(types["user"] == User)
