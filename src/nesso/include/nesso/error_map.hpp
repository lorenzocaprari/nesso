// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_ERROR_MAP_HPP
#define NESSO_ERROR_MAP_HPP

#include <nesso/nesso.hpp>

#include <core/core_types.hpp>
#include <embed/embed_types.hpp>
#include <parser/log_chunker.hpp>

namespace nesso
{

[[nodiscard]] ErrorCause CauseOf(nesso::parser::ParseError error);
[[nodiscard]] ErrorCause CauseOf(nesso::embed::EmbedError error);
[[nodiscard]] ErrorCause CauseOf(nesso::core::EngineError error);

} // namespace nesso

#endif // NESSO_ERROR_MAP_HPP
