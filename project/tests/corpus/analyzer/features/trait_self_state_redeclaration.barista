trait Owned:
	var owner: Self


class User:
	uses Owned

	var owner: User


func test() -> void:
	var user := User.new()
	user.owner = user
	print(user.owner is User)
