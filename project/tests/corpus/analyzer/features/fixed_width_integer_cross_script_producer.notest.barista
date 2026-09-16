# Producer half of the cross-script fixed-width integer analysis fixtures: every public integer type
# appears in a member, a signature, a typed container, and a signal parameter, so a consumer compiled
# from a separate file resolves them through the compiled cross-script metadata rather than inline.
extends RefCounted

signal reported(narrow: uint, wide: ulong)

var narrow_unsigned: uint = 1U
var wide_unsigned: ulong = 1UL
var wide_unsigned_list: Array[ulong] = []


func take_narrow(value: uint) -> uint:
	return value


func take_wide(value: ulong) -> ulong:
	return value
