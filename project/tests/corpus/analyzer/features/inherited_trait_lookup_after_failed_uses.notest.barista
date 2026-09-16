trait Available:
	func inherited_value() -> int:
		return 42


class Root uses Available:
	pass


class BrokenMiddle extends Root:
	uses MissingTrait


class Leaf extends BrokenMiddle:
	func read() -> int:
		return inherited_value()
