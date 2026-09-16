# Alias visibility is lexical and file-local: a base class's alias declaration is not inherited,
# so it is not visible as a bare name at a derived class's explicit type-argument use site, and
# the fallthrough still reports the ordinary unresolved-name diagnostic.
class Base:
	type Hidden = int | uint


class Box[T]:
	var value: T


class Derived extends Base:
	var boxed = Box[Hidden].new()


func test():
	print(Derived.new())
