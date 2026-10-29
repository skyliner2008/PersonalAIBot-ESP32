/**
 * @file TextFeatures.h
 * @brief Language-agnostic lexical features for on-device memory search
 * @see wiki/08-Long-Term-Memory-GraphRAG.md
 *
 * Thai is written without spaces, so word tokenisation needs a dictionary
 * that does not fit comfortably on the MCU. Instead every text is turned
 * into a set of hashed features:
 *   - Thai / other scripts: overlapping character bigrams ("สุนัข" -> สุ ุน นั ัข)
 *   - Latin letters/digits: whole lower-cased words ("iPhone" -> "iphone")
 * Tone marks are dropped so that small transcription differences still match.
 * The resulting sorted hash sets feed a BM25 ranker (see MemoryStore).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../util/PsramAlloc.h"

namespace features {

using HashSet = PsVector<uint32_t>;

/**
 * Extract the sorted, de-duplicated feature hashes of `text`.
 * @param tokenCount  receives the number of features before de-duplication
 *                    (document length for BM25); may be nullptr
 */
void extract(const char* text, size_t len, HashSet& out, uint16_t* tokenCount = nullptr);

/** Normalised form for substring matching of entity names (lower-case, no tone marks, single spaces). */
void normalize(const char* text, size_t len, PsString& out);

/** |a ∩ b| for two sorted hash sets. */
size_t intersectCount(const HashSet& a, const HashSet& b);

/** Jaccard similarity |a ∩ b| / |a ∪ b| (0 when both are empty). */
float jaccard(const HashSet& a, const HashSet& b);

/** Binary search in a sorted hash set. */
bool contains(const HashSet& set, uint32_t h);

}  // namespace features
