#ifndef SCRAN_CENTER_SIZE_FACTORS_HPP
#define SCRAN_CENTER_SIZE_FACTORS_HPP

#include "tatami_stats/tatami_stats.hpp"

#include <vector>
#include <numeric>
#include <algorithm>
#include <type_traits>

#include "sanitize_size_factors.hpp"

/**
 * @file center_size_factors.hpp
 * @brief Center size factors prior to scaling normalization.
 */

namespace scran {

/**
 * @namespace scran::center_size_factors
 * @brief Center size factors prior to scaling normalization.
 *
 * Centering involves scaling all size factors so that their mean is equal to 1.
 * The aim is to ensure that the normalized expression values are on roughly the same scale as the original counts.
 * This simplifies interpretation and ensures that any added pseudo-count prior to log-transformation has a predictable shrinkage effect.
 * In general, size factors should be centered before calling `log_normalize_counts::compute()`.
 *
 * For datasets with multiple blocks, `compute_blocked()` will compute the center separately for each block.
 * The strategy for centering across blocks is determined by `Options::block_mode`.
 */
namespace center_size_factors {

/**
 * Strategy for handling blocks, see `Options::block_mode` for details.
 */
enum class BlockMode : char { PER_BLOCK, LOWEST };

/**
 * @brief Options for `compute()` and `compute_blocked()`.
 */
struct Options {
    /**
     * Strategy for handling blocks in `compute_blocked()`.
     *
     * With the `PER_BLOCK` strategy, size factors are scaled separately for each block so that they have a mean of 1 within each block.
     * The scaled size factors are identical to those obtained by separate invocations of `compute()` on the size factors for each block.
     * This can be desirable to ensure consistency with independent analyses of each block - otherwise, the centering would depend on the size factors across all blocks.
     * However, any systematic differences in the size factors between blocks are lost, i.e., systematic changes in coverage between blocks will not be normalized.
     * 
     * With the `LOWEST` strategy, we compute the mean size factor for each block and we divide all size factors by the minimum mean.
     * In effect, we normalize across blocks by downscale all blocks to match the coverage of the lowest-coverage block.
     * This is useful for datasets with highly heterogeneous coverage of different blocks as it avoids egregious upscaling of low-coverage blocks.
     * (By contrast, downscaling is always safe as it simply discards information across all blocks by shrinking log-fold changes towards zero at low expression.) 
     */
    BlockMode block_mode = BlockMode::LOWEST;

    /**
     * Whether to ignore invalid size factors when computing the mean size factor.
     * Size factors of infinity and NaN or those with non-positive values may occur in datasets that have not been properly filtered to remove low-quality cells.
     * If such values might be present, we can check for and ignore them during the mean calculations.
     *
     * Note that this setting does not actually remove any of the invalid size factors.
     * If these are present, users should call `sanitize_size_factors::compute()` after centering.
     * The `diagnostics` value in `compute()` and `compute_blocked()` can be used to determine whether such a call is necessary.
     * (In general, sanitization should be performed after centering so that the replacement size factors do not interfere with the mean calculations.)
     *
     * If users know that invalid size factors cannot be present, they can set this flag to false for greater efficiency.
     */
    bool ignore_invalid = true;
};

/**
 * @tparam SizeFactor_ Floating-point type for the size factors.
 *
 * @param num Number of cells.
 * @param[in] size_factors Pointer to an array of length `num`, containing the size factor for each cell.
 * @param[out] diagnostics Diagnostics for invalid size factors.
 * This is only used if `Options::ignore_invalid = true`, in which case it is filled with invalid diagnostics for values in `size_factors`.
 * It can also be NULL, in which case it is ignored.
 * @param options Further options.
 *
 * @return The mean size factor.
 */
template<typename SizeFactor_>
SizeFactor_ compute_mean(size_t num, const SizeFactor_* size_factors, sanitize_size_factors::Diagnostics* diagnostics, const Options& options) {
    static_assert(std::is_floating_point<SizeFactor_>::value);
    SizeFactor_ mean = 0;
    size_t denom = 0;

    if (options.ignore_invalid) {
        sanitize_size_factors::Diagnostics tmpdiag;
        auto& diag = (diagnostics == NULL ? tmpdiag : *diagnostics);
        for (size_t i = 0; i < num; ++i) {
            auto val = size_factors[i];
            if (!sanitize_size_factors::internal::is_invalid(val, diag)) {
                mean += val;
                ++denom;
            }
        }
    } else {
        mean = std::accumulate(size_factors, size_factors + num, static_cast<SizeFactor_>(0));
        denom = num;
    }

    if (denom) {
        return mean/denom;
    } else {
        return 0;
    }
}

/**
 * @tparam SizeFactor_ Floating-point type for the size factors.
 *
 * @param num Number of cells.
 * @param[in,out] size_factors Pointer to an array of length `num`, containing the size factor for each cell.
 * On output, this contains centered size factors.
 * @param[out] diagnostics Diagnostics for invalid size factors.
 * This is only used if `Options::ignore_invalid = true`, in which case it is filled with invalid diagnostics for values in `size_factors`.
 * It can also be NULL, in which case it is ignored.
 * @param options Further options.
 *
 * @return The mean size factor.
 */
template<typename SizeFactor_>
SizeFactor_ compute(size_t num, SizeFactor_* size_factors, sanitize_size_factors::Diagnostics* diagnostics, const Options& options) {
    auto mean = compute_mean(num, size_factors, diagnostics, options);
    if (mean) {
        for (size_t i = 0; i < num; ++i){
            size_factors[i] /= mean;
        }
    }
    return mean;
}

/**
 * @tparam SizeFactor_ Floating-point type for the size factors.
 * @tparam Block_ Integer type for the block assignments.
 *
 * @param num Number of cells.
 * @param[in] size_factors Pointer to an array of length `num`, containing the size factor for each cell.
 * @param[in] block Pointer to an array of length `num`, containing the block assignment for each cell.
 * Each assignment should be an integer in \f$[0, N)\f$ where \f$N\f$ is the total number of blocks.
 * @param[out] diagnostics Diagnostics for invalid size factors.
 * This is only used if `Options::ignore_invalid = true`, in which case it is filled with invalid diagnostics for values in `size_factors`.
 * It can also be NULL, in which case it is ignored.
 * @param options Further options.
 *
 * @return Vector of length \f$N\f$ containing the mean size factor for each block.
 */
template<typename SizeFactor_, typename Block_>
std::vector<SizeFactor_> compute_blocked_mean(size_t num, const SizeFactor_* size_factors, const Block_* block, sanitize_size_factors::Diagnostics* diagnostics, const Options& options) {
    static_assert(std::is_floating_point<SizeFactor_>::value);
    size_t ngroups = tatami_stats::total_groups(block, num);
    std::vector<SizeFactor_> group_mean(ngroups);
    std::vector<size_t> group_num(ngroups);

    if (options.ignore_invalid) {
        sanitize_size_factors::Diagnostics tmpdiag;
        auto& diag = (diagnostics == NULL ? tmpdiag : *diagnostics);
        for (size_t i = 0; i < num; ++i) {
            auto val = size_factors[i];
            if (!sanitize_size_factors::internal::is_invalid(val, diag)) {
                auto b = block[i];
                group_mean[b] += val;
                ++(group_num[b]);
            }
        }
    } else {
        for (size_t i = 0; i < num; ++i) {
            auto b = block[i];
            group_mean[b] += size_factors[i];
            ++(group_num[b]);
        }
    }

    for (size_t g = 0; g < ngroups; ++g) {
        if (group_num[g]) {
            group_mean[g] /= group_num[g];
        }
    }

    return group_mean;
}

/**
 * @tparam SizeFactor_ Floating-point type for the size factors.
 * @tparam Block_ Integer type for the block assignments.
 *
 * @param num Number of cells.
 * @param[in] size_factors Pointer to an array of length `num`, containing the size factor for each cell.
 * On output, this contains size factors that are centered according to `Options::block_mode`.
 * @param[in] block Pointer to an array of length `num`, containing the block assignment for each cell.
 * Each assignment should be an integer in \f$[0, N)\f$ where \f$N\f$ is the total number of blocks.
 * @param[out] diagnostics Diagnostics for invalid size factors.
 * This is only used if `Options::ignore_invalid = true`, in which case it is filled with invalid diagnostics for values in `size_factors`.
 * It can also be NULL, in which case it is ignored.
 * @param options Further options.
 *
 * @return Vector of length \f$N\f$ containing the mean size factor for each block.
 */
template<typename SizeFactor_, typename Block_>
std::vector<SizeFactor_> compute_blocked(size_t num, SizeFactor_* size_factors, const Block_* block, sanitize_size_factors::Diagnostics* diagnostics, const Options& options) {
    auto group_mean = compute_blocked_mean(num, size_factors, block, diagnostics, options);

    if (options.block_mode == BlockMode::PER_BLOCK) {
        for (size_t i = 0; i < num; ++i) {
            const auto& div = group_mean[block[i]];
            if (div) {
                size_factors[i] /= div;
            }
        }

    } else if (options.block_mode == BlockMode::LOWEST) {
        SizeFactor_ min = 0;
        bool found = false;
        for (auto m : group_mean) {
            // Ignore groups with means of zeros, either because they're full
            // of zeros themselves or they have no cells associated with them.
            if (m) {
                if (!found || m < min) {
                    min = m;
                    found = true;
                }
            }
        }

        if (min > 0) {
            for (size_t i = 0; i < num; ++i) {
                size_factors[i] /= min;
            }
        }
    }

    return group_mean;
}

}

}

#endif
