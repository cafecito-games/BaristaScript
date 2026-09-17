# A foreign class (`RtcStaticWidget`) is retroactively conformed to `RtcBuildable`, whose only
# requirement is a *static* method. The witness is supplied externally as `static func`, so it is
# reachable only through the target type. This fixture proves a static witness resolves and dispatches
# through `RtcStaticWidget.build_tag()`, and that its body binds against the target's own static
# member.
extend RtcStaticWidget uses RtcBuildable:
	static func build_tag() -> String:
		return prefix + "-tag"


func test() -> void:
	print(RtcStaticWidget.build_tag())
	print(RtcStaticWidget.new() is RtcBuildable)
