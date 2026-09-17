# Static witnesses on inner-class targets. The root class and both inner classes carry a same-named
# static member and are conformed separately, so each witness must resolve and dispatch through the
# class it was declared for rather than through the script path all three share.
extend RtcStaticKits uses RtcBuildable:
	static func build_tag() -> String:
		return prefix + "-tag"


extend RtcStaticKits.Alpha uses RtcBuildable:
	static func build_tag() -> String:
		return prefix + "-tag"


extend RtcStaticKits.Beta uses RtcBuildable:
	static func build_tag() -> String:
		return prefix + "-tag"


func test() -> void:
	print(RtcStaticKits.build_tag())
	print(RtcStaticKits.Alpha.build_tag())
	print(RtcStaticKits.Beta.build_tag())
