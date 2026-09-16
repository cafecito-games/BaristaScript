# Custom annotations from an imported annotation-only namespace resolve by short name and
# validate against the imported declaration's signature.
import cafecito.annotation_index

@suite(name = "Imported")
class Suite:
	@fixture
	var bar: int

	@index_test
	func scenario() -> void:
		pass

func test() -> void:
	pass
