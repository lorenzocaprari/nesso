// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_ERROR_MAP_HPP
#define NESSO_ERROR_MAP_HPP

#include "nesso.hpp"

#include <core/core_types.hpp>
#include <embed/embed_types.hpp>
#include <parser/log_chunker.hpp>

namespace nesso
{

[[nodiscard]] ErrorCause causeOf(parser::ParseError error);
[[nodiscard]] ErrorCause causeOf(embed::EmbedError error);
[[nodiscard]] ErrorCause causeOf(core::EngineError error);

} // namespace nesso

#endif // NESSO_ERROR_MAP_HPP
