trait Storage[T]:
	var value: T
	signal changed(item: T)
	var callback: Callable[[T], T]

	func store(item: T) -> void:
		value = item

	func fetch() -> T:
		return value

	func identity(item: T) -> T:
		return item

	func echo[U](item: U) -> U:
		return item


trait Forward[T] uses Storage[T]:
	func forwarded() -> T:
		return fetch()


class Base[T] uses Forward[T]:
	pass


class Middle[T] extends Base[T]:
	pass


class Child extends Middle[int]:
	func via_bare(item: int) -> int:
		store(item)
		return forwarded()

	func via_self(item: int) -> int:
		self.value = item
		return self.fetch()


func test() -> void:
	var child: Child
	var concrete: int = child.identity(1)
	child.changed.emit(2)
	child.callback = child.identity
	var called: int = child.callback.call(3)
	var generic: String = child.echo[String]("ok")
