# Generic trait declarations parse: inline `trait Name[T]` (with bounds), and `uses Name[Arg]`
# applying type arguments at the use site. This is a parser-level feature check.
trait Bucket[T]:
	abstract func add(item: T) -> void


trait Bounded[T: RefCounted]:
	abstract func keep(item: T) -> T


class IntBucket uses Bucket[int]:
	func add(_item: int) -> void:
		pass


class RefBox uses Bounded[RefCounted]:
	func keep(item: RefCounted) -> RefCounted:
		return item


func test():
	var _a = IntBucket.new()
	var _b = RefBox.new()
	print("generic trait declarations parsed")
