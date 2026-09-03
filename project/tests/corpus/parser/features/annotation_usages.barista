# Custom (non-built-in) annotation usages are preserved by the parser and resolved by the
# analyzer against declarations visible in the current namespace. The declarations below make
# every usage in this file valid so it parses, analyzes, and runs.
namespace cafecito.usages_demo

annotation suite(name: String = "") targets CLASS
annotation tags(...names: String) targets CLASS, METHOD
annotation fixture targets VARIABLE
annotation marker targets METHOD
annotation timeout(seconds: float) targets METHOD
annotation cases(provider: String, count: int = 0) targets METHOD
annotation repeatable(value: String) targets METHOD
annotation mixed(first: int, second: int, label: String = "") targets METHOD

@suite(name = "Combat System")
@tags("gameplay", "slow")
class Combat:
	@fixture
	var world: int = 0

	@marker
	@timeout(10.0)
	@cases(provider = "crit_rows", count = 3)
	func crit(row: int) -> int:
		return row

@repeatable("a")
@repeatable("b")
@mixed(1, 2, label = "x")
func decorated() -> String:
	return "ok"

func test():
	print(decorated())
	print(Combat.new().crit(7))
