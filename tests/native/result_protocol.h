/**************************************************************************/
/*  result_protocol.h                                                     */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once
// Both producer and Python supervisor consume these literals.
#define BS_NATIVE_RESULT_PREFIX "BS_NATIVE_RESULT "
#define BS_NATIVE_PROTOCOL_VERSION 2

// Complete wire shape: producer renders this format; verifier derives its exact field set.
#define BS_NATIVE_RESULT_FORMAT "{\"protocol\":%d,\"suite\":\"%s\",\"case\":\"%s\",\"nonce\":\"%s\",\"build_id\":\"%s\",\"build_info\":%s,\"cases\":%d,\"assertions\":%d,\"failed_cases\":%d,\"failed_assertions\":%d}"
