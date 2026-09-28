// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "execute.hpp"

#include "output.hpp"

#include <expected>

namespace nesso::cli
{

template <typename Result, typename... Context>
static int report(const std::expected<Result, Error> &outcome, const Context &...context)
{
    return outcome ? render(*outcome, context...) : renderError(outcome.error());
}

int execute(const Nesso &nesso, const GrepRequest &request) { return report(nesso.grep(request)); }

int execute(const Nesso &nesso, const IndexRequest &request) { return report(nesso.index(request), request); }

int execute(const Nesso &nesso, const SearchRequest &request) { return report(nesso.search(request)); }

int execute(const Nesso &nesso, const StoreInitRequest &request)
{
    announce(request);
    return report(nesso.storeInit(request));
}

int execute(const Nesso &nesso, const StoreIngestRequest &request)
{
    announce(request);
    return report(nesso.storeIngest(request));
}

int execute(const Nesso &nesso, const StoreSearchRequest &request) { return report(nesso.storeSearch(request)); }

} // namespace nesso::cli
