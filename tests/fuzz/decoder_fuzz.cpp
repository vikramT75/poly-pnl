#include <cstdint>
#include <cstddef>
#include <cstring>
#include "decoder.h"
#include "intern_table.h"

using namespace gravia;
using namespace gravia::decoder;

namespace {

WalletInternTable g_wallet_table;
TokenInternTable  g_token_table;

WalletIdx fuzzer_wallet_cb(const uint8_t addr[20], void*) noexcept {
    return g_wallet_table.intern(addr);
}

TokenIdx fuzzer_token_cb(const uint8_t tok[32], void*) noexcept {
    return g_token_table.intern(tok);
}

InternTables g_intern_tables{fuzzer_wallet_cb, fuzzer_token_cb, nullptr};
Decoder g_decoder(g_intern_tables);

} // namespace

// libFuzzer entry point
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 40) return 0; // minimum header requirement

    RawLog log{};
    // Construct RawLog from fuzz input safely
    size_t copy_len = size > sizeof(RawLog) ? sizeof(RawLog) : size;
    std::memcpy(&log, data, copy_len);

    // Bound values so memory access remains safe
    if (log.num_topics > MAX_TOPICS) log.num_topics = MAX_TOPICS;
    if (log.data_len > MAX_DATA_BYTES) log.data_len = MAX_DATA_BYTES;

    NormalizedEvent out_events[Decoder::MAX_EVENTS_PER_LOG];
    // Must never crash or read OOB
    int res = g_decoder.decode(log, out_events, Decoder::MAX_EVENTS_PER_LOG);
    (void)res;

    return 0;
}
