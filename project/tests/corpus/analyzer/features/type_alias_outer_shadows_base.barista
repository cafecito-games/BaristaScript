# A base class declaring an alias name does not hide the lexically visible alias of the same name:
# aliases are not inherited, so the base's declaration is simply not a candidate here.
type Tag = String


class Base:
	type Tag = int


class Derived extends Base:
	var tag: Tag = "outer"


func test():
	print(Derived.new().tag)
