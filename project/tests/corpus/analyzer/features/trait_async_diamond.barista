# An async required method reached through two trait paths (diamond) is a single
# requirement: one async implementation satisfies it for every path.
extends RefCounted
uses Left, Right

trait Fetchable:
	abstract async func fetch() -> String

trait Left:
	uses Fetchable

trait Right:
	uses Fetchable

async func fetch() -> String:
	return "data"

func test() -> void:
	pass
