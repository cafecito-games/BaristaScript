/**************************************************************************/
/*  parser_fixtures.h                                                     */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

namespace barista_script::test {
// Byte-identical to the legacy parser suite at 1fe5964.
inline const char *rich_source() {
	return R"BARISTA(@icon("res://icon.svg")
namespace game.core
import engine

class_name Rich
extends RefCounted

signal changed(value: int)

const LIMIT := 10

enum Mode:
	IDLE = 0
	BUSY = 1

type Numbers = Array[int]

tuple Pair(first: int, second: int)

class Inner:
	var depth := 0

var items: Array[int] = [1, 2, 3]
var table: Dictionary[String, int] = {"a": 1}
var flag := true

static func make() -> Rich:
	return Rich.new()

func run(count: int = 1) -> int:
	var total := 0
	for index in items:
		if index > LIMIT:
			continue
		elif index < 0:
			break
		else:
			total += index * 2
	while total > 100:
		total -= 1
	match total:
		0:
			pass
		_:
			total = total
	var callback := func(value: int) -> int: return value + 1
	var maybe = total if flag else 0
	var casted = total as float
	var tested = self is Rich
	assert(total >= 0)
	var tuple_value := (1, 2)
	var node_value = $Child
	var loaded = preload("res://tests/parser_fixture.barista")
	total = callback.call(total)
	total = -total
	total = ~total
	total = total + (maybe as int)
	await changed
	return total
)BARISTA";
}
} // namespace barista_script::test
