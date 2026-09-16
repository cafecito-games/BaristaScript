# A component the recorded form cannot identify -- a tuple has no flattened identity -- contributes
# no evidence for itself while its container's own identity is still compared, so the store below
# stays accepted rather than rejected on a component nothing can decide.
trait RcuKeeper[T]:
	abstract func keep(item: T) -> T


class RcuTarget:
	pass


extend RcuTarget uses RcuKeeper[Array[(int, String)]]:
	func keep(item: Array[(int, String)]) -> Array[(int, String)]:
		return item


func test() -> void:
	var unidentified: RcuKeeper[Array[(String, int)]] = RcuTarget.new()
	print(unidentified != null)
