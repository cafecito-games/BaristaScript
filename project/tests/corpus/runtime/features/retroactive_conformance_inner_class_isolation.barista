# A retroactive conformance names exactly one class, but every class compiled from a file reports that
# file's resource path, so the path is an identity only for the file's root class. Treating it as one
# reported an inner class's conformance on its siblings and on the root class, and the root class's
# conformance on its inner classes. Covers same-file and foreign-file targets, the implied supertrait
# (a target that conforms to a supertrait must not gain the subtrait), and an unrelated trait.
trait RtcIsoRoot:
	pass


trait RtcIsoLeaf uses RtcIsoRoot:
	pass


trait RtcIsoHost:
	pass


class LocalAlpha:
	pass


class LocalBeta:
	pass


extend LocalAlpha uses RtcIsoLeaf:
	pass


extend LocalBeta uses RtcIsoRoot:
	pass


extend RtcStaticKits.Alpha uses RtcIsoLeaf:
	pass


extend RtcStaticKits.Beta uses RtcIsoRoot:
	pass


extend RtcStaticKits uses RtcIsoHost:
	pass


func test() -> void:
	var local_alpha: Variant = LocalAlpha.new()
	var local_beta: Variant = LocalBeta.new()

	print(local_alpha is RtcIsoLeaf)
	print(local_alpha is RtcIsoRoot)
	print(is_instance_of(local_alpha, RtcIsoRoot))
	print(local_alpha is RtcIsoHost)

	print(local_beta is RtcIsoRoot)
	print(is_instance_of(local_beta, RtcIsoRoot))
	print(local_beta is RtcIsoLeaf)
	print(local_beta is RtcIsoHost)

	var alpha: Variant = RtcStaticKits.Alpha.new()
	var beta: Variant = RtcStaticKits.Beta.new()
	var host: Variant = RtcStaticKits.new()

	print(alpha is RtcIsoLeaf)
	print(alpha is RtcIsoRoot)
	print(alpha is RtcIsoHost)

	print(beta is RtcIsoRoot)
	print(beta is RtcIsoLeaf)
	print(beta is RtcIsoHost)

	print(host is RtcIsoHost)
	print(host is RtcIsoLeaf)
	print(host is RtcIsoRoot)
