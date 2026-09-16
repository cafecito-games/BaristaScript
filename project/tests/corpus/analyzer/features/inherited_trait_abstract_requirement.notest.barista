trait Pingable:
	abstract func ping() -> int
	abstract func echo[T](value: T) -> T


abstract class Base:
	uses Pingable


func test(base: Base) -> void:
	var direct: int = base.ping()
	var generic: String = base.echo[String]("ok")
	print(direct, generic)
