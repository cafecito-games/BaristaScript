# Target scope wins on collisions. The witness's unqualified `Marker` resolves to the type the foreign
# target's own outer class declares, not to this file's same-named class, while an uncollided
# declaring-file type stays reachable through the fallback.
extend RtcScopeCollide.Inner uses RtcScopeTagging:
	func tag() -> String:
		var marker: Marker = Marker.new()
		var local: OnlyHere = OnlyHere.new()
		return marker.tag() + "/" + local.tag() + "/" + str(seed)


trait RtcScopeTagging:
	abstract func tag() -> String


class Marker:
	func tag() -> String:
		return "conformance-marker"


class OnlyHere:
	func tag() -> String:
		return "only-here"


func test() -> void:
	var tagged: RtcScopeTagging = RtcScopeCollide.Inner.new()
	print(tagged.tag())
