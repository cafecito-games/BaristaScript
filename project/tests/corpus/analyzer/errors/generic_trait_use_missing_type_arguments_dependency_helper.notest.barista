# Helper for generic_trait_use_missing_type_arguments_dependency. The bare generic `uses` on
# `Inner` is reported by this file's own analysis; a consumer that resolves this class first must
# not absorb the diagnostic as its own trait-use failure.
class_name CafecitoBareUseHelper
extends RefCounted

trait Storage[T]:
	func stored(value: T) -> T:
		return value


class Inner:
	uses Storage
