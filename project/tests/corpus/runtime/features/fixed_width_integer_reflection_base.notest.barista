# Base half of the cross-script fixed-width integer reflection fixture, so an inherited member, an
# inherited signature, and an overridden signature can each be checked for exact width retention
# through a script boundary.
extends RefCounted

var inherited_wide_unsigned: ulong = 1UL


func inherited_only(value: ulong) -> ulong:
	return value


func overridable(value: uint) -> uint:
	return value
