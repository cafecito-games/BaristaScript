# Companion trait for the retroactive-conformance static-witness fixtures. Its single requirement is
# a *static* method, so a conforming target must supply the witness as `static func` and callers reach
# it through the target type rather than through an instance.
trait_name RtcBuildable

abstract static func build_tag() -> String
