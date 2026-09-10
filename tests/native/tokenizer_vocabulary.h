/**************************************************************************/
/*  tokenizer_vocabulary.h                                                */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once
// Exact KITCHEN_SINK input from tokenizer_test.gd at 71d7883. Plain bytes only at static initialization.
inline constexpr const char *TOKENIZER_VOCABULARY = R"BS(@export
@cafecito.test.timeout
namespace app
import other
abstract class Sink extends Node:
	class_name Sink
	trait_name Drainable
	enum_name Kind
	tuple_name Pair
	trait Drainable:
		pass
	tuple Pair:
		pass
	enum Kind:
		pass
	signal drained
	const LIMIT = 10
	static final var shared = 1
	uses Drainable
	var text = "regular"
	var raw = r"raw\"quoted"
	var name = &"string_name"
	var path = ^"node/path"
	var block = """multi
	line"""
	var numbers = [1, 2.5, 0x1f, 0b1010, 1_000, .5, 1e3]
	var constants = [PI, TAU, INF, NAN]
	var node = $Child
	var wildcard = _
	var nullable: int? = null
	var spread = [...numbers]
	var slice = numbers[0..2]
	var mapping = {"key": 1, "other": 2}
	func drain(value: int, ...rest: Array) -> void:
		if value == 1 and not value != 2 or value < 3 or value > 4:
			value += 1
			value -= 1
			value *= 2
			value **= 2
			value /= 2
			value %= 2
			value <<= 1
			value >>= 1
			value &= 1
			value |= 1
			value ^= 1
			value = value + 1 - 1 * 2 ** 2 / 2 % 2
			value = value & 1 | 2 ^ 3 << 1 >> 1
			value = ~value
			value = value && true || false
			value = value <= 3 >= 1
			value = self.drain
		elif value is int:
			var cast = value as int
			await drained
			assert(value)
			breakpoint
			yield
			var loaded = preload("res://other.barista")
			super.drain(value)
		else:
			for index in numbers:
				while value:
					break
				continue
			match value:
				1 when value:
					pass
				_:
					pass
		return
	func tuple_index(pair: Pair) -> void:
		var first = pair.0
		var second = pair.1;
	func void_returning() -> void:
		pass
`
)BS"
													"=======\n1abc\n";
