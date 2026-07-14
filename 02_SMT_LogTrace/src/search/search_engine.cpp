/**
 * @file search_engine.cpp
 * @brief 实现跨 Segment 低 DF AND、BM25、业务权重和 Top-K。
 */

#include "logtrace/search/search_engine.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <vector>

#include "logtrace/indexing/original_log_reader.h"
#include "logtrace/indexing/term_tokenizer.h"

namespace smt {
namespace logtrace {
namespace {

const double kBm25K1 = 1.2;
const double kBm25B = 0.75;

bool isBetter(const SearchHit& left, const SearchHit& right) {
    if (std::fabs(left.score - right.score) > 1e-12) {
        return left.score > right.score;
    }
    if (left.document.occurred_at_ms != right.document.occurred_at_ms) {
        return left.document.occurred_at_ms > right.document.occurred_at_ms;
    }
    return left.document.doc_id > right.document.doc_id;
}

struct BetterHit {
    bool operator()(const SearchHit& left, const SearchHit& right) const {
        return isBetter(left, right);
    }
};

std::vector<std::string> normalizedTerms(const std::vector<std::string>& keywords) {
    std::set<std::string> unique;
    for (std::vector<std::string>::const_iterator keyword = keywords.begin();
         keyword != keywords.end(); ++keyword) {
        const std::vector<std::string> terms = tokenizeTerms(*keyword);
        unique.insert(terms.begin(), terms.end());
    }
    return std::vector<std::string>(unique.begin(), unique.end());
}

bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool matchesFilters(const SegmentDocumentRecord& document, const SearchQuery& query) {
    if ((!query.line_id.empty() && document.line_id != query.line_id) ||
        (!query.station_id.empty() && document.station_id != query.station_id) ||
        (!query.device_id.empty() && document.device_id != query.device_id) ||
        (!query.work_order.empty() && document.work_order != query.work_order) ||
        (!query.product_sn.empty() && document.product_sn != query.product_sn) ||
        (!query.levels.empty() && !contains(query.levels, document.level)) ||
        (!query.module_name.empty() && document.module_name != query.module_name) ||
        (!query.error_code.empty() && document.error_code != query.error_code) ||
        (query.has_time_range && (document.occurred_at_ms < query.occurred_from_ms ||
                                  document.occurred_at_ms > query.occurred_to_ms))) {
        return false;
    }
    return !query.anomaly_only || document.level == "WARN" || document.level == "ERROR" ||
           !document.error_code.empty();
}

std::uint32_t termFrequency(const LoadedSegment& segment, const SegmentTermRecord& term,
                            std::uint32_t local_id) {
    std::size_t left = static_cast<std::size_t>(term.posting_begin);
    std::size_t right = left + term.posting_count;
    while (left < right) {
        const std::size_t middle = left + (right - left) / 2;
        const SegmentPosting& posting = segment.postings[middle];
        if (posting.local_id < local_id) {
            left = middle + 1;
        } else {
            right = middle;
        }
    }
    return left < segment.postings.size() && segment.postings[left].local_id == local_id
               ? segment.postings[left].term_frequency
               : 0;
}

double businessWeight(const SegmentDocumentRecord& document, const SearchQuery& query) {
    double score = 0.0;
    if (!query.error_code.empty() && document.error_code == query.error_code) score += 3.0;
    if (!query.module_name.empty() && document.module_name == query.module_name) score += 1.5;
    if (document.level == "ERROR") score += 2.0;
    if (document.level == "WARN") score += 1.0;
    return score;
}

}  // namespace

SearchEngine::SearchEngine(const IndexSnapshotStore& snapshots, const StoragePaths& storage)
    : snapshots_(snapshots), storage_(storage) {}

SearchPage SearchEngine::search(const SearchQuery& query) const {
    if (query.page_size == 0 || query.offset > 1000 || query.page_size > 1000 - query.offset) {
        throw std::invalid_argument("search pagination exceeds internal contract");
    }
    const std::shared_ptr<const IndexSnapshot> snapshot = snapshots_.current();
    const std::vector<std::string> terms = normalizedTerms(query.keywords);
    const std::size_t document_count = snapshot->documentCount();
    std::map<std::string, std::size_t> global_df;
    std::uint64_t total_length = 0;
    for (std::vector<std::shared_ptr<const LoadedSegment> >::const_iterator segment =
             snapshot->segments().begin();
         segment != snapshot->segments().end(); ++segment) {
        for (std::vector<std::string>::const_iterator term = terms.begin(); term != terms.end();
             ++term) {
            const std::map<std::string, std::size_t>::const_iterator found =
                (*segment)->term_lookup.find(*term);
            if (found != (*segment)->term_lookup.end()) {
                global_df[*term] += (*segment)->terms[found->second].document_frequency;
            }
        }
        for (std::vector<SegmentDocumentRecord>::const_iterator document =
                 (*segment)->documents.begin();
             document != (*segment)->documents.end(); ++document) {
            total_length += document->term_count;
        }
    }
    const double average_length = document_count == 0 ? 1.0
                                                      : static_cast<double>(total_length) /
                                                            static_cast<double>(document_count);
    const std::size_t capacity = query.offset + query.page_size;
    std::priority_queue<SearchHit, std::vector<SearchHit>, BetterHit> best;
    std::size_t total_hits = 0;

    for (std::vector<std::shared_ptr<const LoadedSegment> >::const_iterator segment_ptr =
             snapshot->segments().begin();
         segment_ptr != snapshot->segments().end(); ++segment_ptr) {
        const LoadedSegment& segment = **segment_ptr;
        std::vector<const SegmentTermRecord*> segment_terms;
        bool missing = false;
        for (std::vector<std::string>::const_iterator term = terms.begin(); term != terms.end();
             ++term) {
            const std::map<std::string, std::size_t>::const_iterator found =
                segment.term_lookup.find(*term);
            if (found == segment.term_lookup.end()) {
                missing = true;
                break;
            }
            segment_terms.push_back(&segment.terms[found->second]);
        }
        if (missing) continue;
        std::sort(segment_terms.begin(), segment_terms.end(),
                  [](const SegmentTermRecord* left, const SegmentTermRecord* right) {
                      return left->document_frequency < right->document_frequency;
                  });
        std::vector<std::uint32_t> candidates;
        if (segment_terms.empty()) {
            for (std::size_t index = 0; index < segment.documents.size(); ++index) {
                candidates.push_back(static_cast<std::uint32_t>(index));
            }
        } else {
            const SegmentTermRecord& first = *segment_terms.front();
            for (std::uint32_t index = 0; index < first.posting_count; ++index) {
                candidates.push_back(
                    segment.postings[static_cast<std::size_t>(first.posting_begin + index)]
                        .local_id);
            }
            for (std::size_t term_index = 1; term_index < segment_terms.size(); ++term_index) {
                const SegmentTermRecord& term = *segment_terms[term_index];
                std::vector<std::uint32_t> postings;
                for (std::uint32_t index = 0; index < term.posting_count; ++index) {
                    postings.push_back(
                        segment.postings[static_cast<std::size_t>(term.posting_begin + index)]
                            .local_id);
                }
                std::vector<std::uint32_t> intersection;
                std::set_intersection(candidates.begin(), candidates.end(), postings.begin(),
                                      postings.end(), std::back_inserter(intersection));
                candidates.swap(intersection);
                if (candidates.empty()) break;
            }
        }
        for (std::vector<std::uint32_t>::const_iterator local = candidates.begin();
             local != candidates.end(); ++local) {
            const SegmentDocumentRecord& document = segment.documents[*local];
            if (!matchesFilters(document, query)) continue;
            double score = businessWeight(document, query);
            for (std::size_t index = 0; index < terms.size(); ++index) {
                const SegmentTermRecord& term = *segment_terms[index];
                const double frequency = termFrequency(segment, term, *local);
                const double df = static_cast<double>(global_df[term.term]);
                const double idf =
                    std::log(1.0 + (static_cast<double>(document_count) - df + 0.5) / (df + 0.5));
                const double normalization =
                    1.0 - kBm25B + kBm25B * document.term_count / average_length;
                score += idf * frequency * (kBm25K1 + 1.0) / (frequency + kBm25K1 * normalization);
            }
            ++total_hits;
            const SearchHit hit{document, score};
            if (best.size() < capacity) {
                best.push(hit);
            } else if (isBetter(hit, best.top())) {
                best.pop();
                best.push(hit);
            }
        }
    }
    std::vector<SearchHit> ranked;
    while (!best.empty()) {
        ranked.push_back(best.top());
        best.pop();
    }
    std::sort(ranked.begin(), ranked.end(), isBetter);
    SearchPage page{snapshot->version(), total_hits, std::vector<SearchHit>()};
    const std::size_t end = std::min(ranked.size(), capacity);
    for (std::size_t index = query.offset; index < end; ++index)
        page.items.push_back(ranked[index]);
    return page;
}

LogDetail SearchEngine::detail(std::uint64_t doc_id) const {
    const std::shared_ptr<const IndexSnapshot> snapshot = snapshots_.current();
    const SegmentDocumentRecord* document = nullptr;
    const SegmentFileRecord* file = nullptr;
    if (!snapshot->findDocument(doc_id, &document, &file)) {
        throw std::out_of_range("document is not present in current READY snapshot");
    }
    return LogDetail{
        *document, *file,
        readOriginalRecord(storage_, *file, document->byte_offset, document->byte_length)};
}

}  // namespace logtrace
}  // namespace smt
