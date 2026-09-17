trait Factory extends RefCounted:
	abstract static func create() -> Self


class User extends RefCounted:
	uses Factory

	static func create() -> User:
		return User.new()


class Admin extends RefCounted:
	uses Factory

	static func create() -> Admin:
		return Admin.new()


func test():
	var factories: Array[Type[Factory]] = [User, Admin]

	# Subscript retains `Type[Factory]`, so the static requirement stays callable.
	print(factories[0].create() is User)

	# `for` loop variables over `Array[Type[T]]` retain the handle type.
	for factory in factories:
		print(factory.create() != null)

	# An inferred local retains the handle type.
	var inferred = factories[1]
	print(inferred.create() is Admin)

	# Element-typed Array methods retain the handle type.
	print(factories.front().create() is User)
	print(factories.back().create() is Admin)
	print(factories.pop_back().create() is Admin)

	var registry: Dictionary[String, Type[Factory]] = { "user": User }
	print(registry["user"].create() is User)
	print(registry.get("user").create() is User)

	# A destructuring binding over a tuple of element reads retains the handle type.
	var pair: (Type[Factory], Type[Factory]) = (factories[0], factories[0])
	var (first, second) = pair
	print(first.create() is User)
	print(second.create() is User)
