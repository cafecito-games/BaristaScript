# An instance async method that builds a `Self`-typed container after `await` must resolve `Self`
# against the receiver's leaf on the resumed frame, not just on the initial frame. The typed local is
# declared and built after the await so its descriptor is resolved on the resume path: before the
# fix that path left `FrameSelfBinding` with no receiver for an instance frame, so the array built
# after the await read back as the declaring class rather than the receiver's leaf. The whole chain
# runs synchronously through one signal emission (the harness calls `test()` once with no main loop).
signal go


class Base:
	async func observe(gate: Signal) -> void:
		await gate
		var out: Array[Self] = []
		out.append(self)
		print("typed as self after await: ", out.get_typed_script() == self.get_script())


class Child:
	extends Base


func test() -> void:
	# A Child receiver must keep resolving `Self` to Child once its frame is resumed; a regression
	# would type the array as the declaring Base and print `false`.
	var child := Child.new()
	child.observe(go)
	go.emit()

	var base := Base.new()
	base.observe(go)
	go.emit()
