# The declaring file for the class-target half of the shared-declaring-file regression, pinning the
# fix for non-builtin conformance targets too. Deliberately shared by
# `conformance_class_witness_shared_declaring_file_a` and `_b` rather than given a private copy each;
# see `csw_shared_int_doubler_ext.notest.fs` for the full explanation of why sharing matters here.
extend CswSharedClassTarget uses CswSharedClassDoubler:
	func csw_shared_class_doubled() -> int:
		return level * 2
