# A `final static var` has a single shared slot, so a qualified write outside its
# `_static_init()` slot is rejected just like a bare-name write.
class Holder:
	final static var VALUE := 1

func test() -> void:
	Holder.VALUE = 2
