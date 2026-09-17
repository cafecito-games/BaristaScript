# A payload-less case is a compile-time constant value, so it can back a `const` and still compares
# equal to a value of the same case obtained anywhere else. A member whose name happens to match a
# case is unaffected: only `Enum.Case`, or a bare case name inside the enum's own declaration, names
# a case.
enum Probe:
	Quit
	Move(x: int, y: int)

const QUIT_ALIAS = Probe.Quit
const QUIT_TYPED: Probe = Probe.Quit

class Holder:
	var Quit: Probe = Probe.Move(1, 2)

func test():
	print(QUIT_ALIAS)
	print(QUIT_TYPED)
	print(QUIT_ALIAS == Probe.Quit)
	print(QUIT_ALIAS == QUIT_TYPED)
	print(QUIT_ALIAS == Probe.Move(1, 2))

	var holder := Holder.new()
	print(holder.Quit)
	holder.Quit = Probe.Quit
	print(holder.Quit == Probe.Quit)
