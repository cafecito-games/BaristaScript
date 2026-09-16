# Producer half of the cross-script fixed-width integer narrowing fixture. Every declaration here is
# wide, so each use in the consumer narrows across the script boundary.
extends RefCounted

signal reported(value: ulong)

var wide_unsigned: ulong = 1UL
var wide_unsigned_list: Array[ulong] = []


func take_narrow(value: uint) -> uint:
	return value


func take_wide(value: ulong) -> ulong:
	return value
