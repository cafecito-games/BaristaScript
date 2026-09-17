trait Owned:
	var owner: Self


class User:
	uses Owned


class Other:
	uses Owned


func test() -> void:
	var user := User.new()
	var other: Variant = Other.new()
	user.owner = other
	print(user.owner)
