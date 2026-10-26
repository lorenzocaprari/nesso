// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef EMBED_AMX_GUARD_HPP
#define EMBED_AMX_GUARD_HPP

namespace nesso::embed
{

/// Stops this process from enabling Intel AMX tile data, so ONNX Runtime keeps
/// its AVX-512 VNNI or AVX2 int8 kernels. Call it before the first session.
/// Returns true when AMX cannot be in use: the CPU is not x86-64, or the
/// permission request is now refused. Returns false when the permission was
/// already granted or the refusal could not be installed.
bool denyAmxTiles();

} // namespace nesso::embed

#endif // EMBED_AMX_GUARD_HPP
