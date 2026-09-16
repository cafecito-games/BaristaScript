trait Creatable:
	abstract func label() -> String


class User:
	uses Creatable

	func label() -> String:
		return "user"


var exact_user_handle: Type[User] = User
var subclass_node_handle: Type[Node] = Button
var trait_user_handle: Type[Creatable] = User


func accept_user(_klass: Type[User]) -> void:
	pass


func accept_node(_klass: Type[Node]) -> void:
	pass


func accept_creatable(_klass: Type[Creatable]) -> void:
	pass


func test():
	accept_user(User)
	accept_node(Button)
	accept_creatable(User)
	print(exact_user_handle == User)
	print(subclass_node_handle == Button)
	print(trait_user_handle == User)
