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

[[nodiscard]] auto CauseOf(nesso::parser::ParseError error) -> ErrorCause;
[[nodiscard]] auto CauseOf(nesso::embed::EmbedError error) -> ErrorCause;
[[nodiscard]] auto CauseOf(nesso::core::EngineError error) -> ErrorCause;

} // namespace nesso

#endif // NESSO_ERROR_MAP_HPP
