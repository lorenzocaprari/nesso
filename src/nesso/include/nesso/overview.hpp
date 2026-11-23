// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_OVERVIEW_HPP
#define NESSO_OVERVIEW_HPP

#include <nesso/nesso.hpp>

#include <core/event_types.hpp>
#include <parser/archive_source.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace nesso
{

inline constexpr size_t DEFAULT_OVERVIEW_TOP_K = 20;

/// Distinct event types `Overview` keeps before it fails with
/// `ErrorCause::TooManyEventTypes`.
inline constexpr size_t DEFAULT_MAX_EVENT_TYPES = 1'000'000;

using EventOrder = nesso::core::EventOrder;

/// Event types of `files`. No model is loaded.
struct OverviewRequest
{
    /// Inputs as `grep` takes them: files, directories, bundles, or `-`.
    std::vector<std::filesystem::path> files;
    size_t top_k = DEFAULT_OVERVIEW_TOP_K;
    std::string json_field = "message";
    EventOrder order = EventOrder::Rare;
    /// Caps for tar and zip bundles. Plain files ignore them.
    nesso::parser::ArchiveLimits archive_limits{};
    size_t max_types = DEFAULT_MAX_EVENT_TYPES;
};

/// One event type: the masked key of its kept lines, how many there are, and
/// the first of them.
struct EventTypeRow
{
    std::uint64_t count = 0;
    /// The first line of the type: path as given, `-`, or `bundle:entry`.
    std::string source;
    std::uint64_t line = 0;
    bool array_element = false;
    std::string text;
    std::string key;
    /// 1-based ordinals over the kept lines of every input, in input order.
    std::uint64_t first_seen = 0;
    std::uint64_t last_seen = 0;
};

struct OverviewResults
{
    /// At most `top_k` rows, in the requested order.
    std::vector<EventTypeRow> rows;
    /// Kept lines and distinct event types across every input.
    std::uint64_t lines = 0;
    std::uint64_t types = 0;
    size_t skipped_lines = 0;
    size_t empty_lines = 0;
    size_t malformed_lines = 0;
    size_t missing_field_lines = 0;
    size_t truncated_lines = 0;
    size_t binary_entries = 0;
    std::string json_field = "message";
    std::vector<BundleSkipSummary> bundle_skips;
};

/// Counts the kept lines of `request.files` per masked dedup key, the key
/// `--group` uses, in one parse. `Rare` puts the fewest lines first, then the
/// type that appeared later. `New` puts the type that appeared later first.
/// "Later" is input order: arguments as given, directories in byte order, so
/// pass files oldest first. No kept line is `ErrorKind::EmptyInput`. More than
/// `max_types` types is `ErrorCause::TooManyEventTypes`.
[[nodiscard]] std::expected<OverviewResults, Error> Overview(const OverviewRequest &request);

} // namespace nesso

#endif // NESSO_OVERVIEW_HPP
