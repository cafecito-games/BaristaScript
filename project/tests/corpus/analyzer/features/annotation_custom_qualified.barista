# Fully qualified annotation usage resolves by its canonical identity without an import. The
# declarations live in the `cafecito.annotation_index` namespace, which this file never imports.
@cafecito.annotation_index.suite(name = "Qualified")
class Suite:
	@cafecito.annotation_index.fixture
	var bar: int

	@cafecito.annotation_index.index_test
	func scenario() -> void:
		pass

func test() -> void:
	pass
