/**************************************************************************/
/*  storage_fixture.h                                                     */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "barista_script_language.h"
#include "bs_analyzer.h"
#include "bs_cache.h"
#include "bs_declaration_index.h"
#include "doctest.h"

namespace barista_script::native_tests {

Vector<uint8_t> bytes(const String &text);
Vector<uint8_t> read_bytes(const String &path);
bool write_bytes(const String &path, const Vector<uint8_t> &data);
Vector<String> temporary_files(const String &store);
/** Count `*.tmp` files directly under `root` (non-recursive), matching GDScript `_count_tmp_under`. */
int count_tmp_under(const String &root);
void append_integer(Vector<uint8_t> &destination, uint64_t value, int width);
bool verify_case_isolation(void (*scenario)());

// One scope redirects host-visible state and confines all files to its own user:// subtree.
class StorageFixture {
	BSCache::ScopedCorpusState cache_scope;
	BSDeclarationIndex::ScopedCorpusState index_scope;
	String previous_bootstrap_root;

public:
	String root;
	StorageFixture();
	~StorageFixture();
	StorageFixture(const StorageFixture &) = delete;
	StorageFixture &operator=(const StorageFixture &) = delete;
	String path(const String &name) const { return root.path_join(name); }
	BSDeclarationIndex &index() const { return BaristaScriptLanguage::get_singleton()->get_declaration_index(); }
};

} // namespace barista_script::native_tests
