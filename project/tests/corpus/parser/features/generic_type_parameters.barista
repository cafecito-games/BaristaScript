# Generic type-parameter declarations parse on classes and methods.
# This is a parser-only feature: the parameters are recorded on the AST but not
# yet resolved as types, so they are never used in a type position here.
class_name GenericDeclarations[T]

class Box[T]:
	var value


class Pair[K, V]:
	var first
	var second


class Bounded[T: RefCounted]:
	var value


class MultiBounded[K, V: RefCounted]:
	var key
	var value


func generic_method[T]():
	pass


func bounded_method[T: RefCounted]():
	pass


func multi_method[K, V: RefCounted]():
	pass


func test():
	var _box = Box.new()
	var _pair = Pair.new()
	var _bounded = Bounded.new()
	var _multi = MultiBounded.new()
	print("generic declarations parsed")
