// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef EMBED_AMX_GUARD_HPP
#define EMBED_AMX_GUARD_HPP

#include <cstdio>

namespace nesso::embed
{

/// Stops this process from enabling Intel AMX tile data, so ONNX Runtime keeps
/// its AVX-512 VNNI or AVX2 int8 kernels. Call it before the first session.
/// Returns true when AMX cannot be in use: the CPU is not x86-64, or the
/// permission request is now refused. Returns false when the permission was
/// already granted or the refusal could not be installed.
bool denyAmxTiles();

/// Writes one line to `err` when `guardInEffect` is false, naming that the int8
/// model may use the AMX kernel. Writes nothing when the guard is in effect.
/// `guardInEffect` is the result of denyAmxTiles(), or a stand-in for tests.
void reportAmxGuard(bool guardInEffect, std::FILE *err);

} // namespace nesso::embed

#endif // EMBED_AMX_GUARD_HPP
