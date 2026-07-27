/*
 * Complexity regression for the centroid sort (MOD-17228).
 *
 * A correctness-only test cannot tell an O(n log n) sort from an O(n^2) one, and the ordinary
 * unit test auto-compresses in small batches so it never sorts one large partition. This test
 * compiles the library with TD_INSTRUMENT_SORT (a key-comparison counter) and asserts the two
 * properties that give td_qsort its worst-case guarantee, each on the input that actually
 * defeats a sort lacking it, across multiple sizes so a quadratic implementation cannot slip
 * under a single hand-picked bound:
 *
 *   Part A - duplicate-heavy input (the reported DoS). N identical values is the MOD-17228
 *            vector: the pre-PR central-pivot Lomuto sort peels one element per level and does
 *            exactly n^2/2 comparisons (~5e9 at n=1e5, a ~98 s server freeze). The 3-way
 *            partition collapses the equal run in a single pass -> exactly 2n comparisons.
 *            We assert a strict LINEAR bound, which a revert to any sort without equal-run
 *            handling misses by three-plus orders of magnitude.
 *
 *   Part B - the heapsort fallback. A crafted distinct permutation can drive a median-of-three
 *            quicksort into deep recursion; the depth-limit -> heapsort fallback is what caps
 *            that at O(n log n). We drive the fallback directly (measured ~1.78 n log2 n) and
 *            assert an O(n log n) bound with the ratio held roughly flat across sizes, so
 *            weakening or removing the fallback (letting the range go quadratic) fails here.
 *
 * Numbers above are measured, not assumed; the asserted constants leave generous margin over
 * them while staying far below quadratic.
 */
#ifndef TD_INSTRUMENT_SORT
/* Also set by target_compile_definitions() in tests/CMakeLists.txt; guard so a standalone
 * compile still instruments without redefining the macro (which -Werror would reject). */
#define TD_INSTRUMENT_SORT 1
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "tdigest.c" /* brings in the static sort helpers + td_sort_comparisons */

static int failures = 0;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: ");                                                             \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n");                                                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* Part A: sort n identical values in a single compress and return the comparison count. */
static double compares_all_equal(int n) {
    /* cap = 6n + 10 > n, so all n points buffer and one td_compress() sorts them together. */
    td_histogram_t *h = td_new((double)n);
    if (h == NULL) {
        fprintf(stderr, "allocation failed at n=%d\n", n);
        exit(1);
    }
    for (int i = 0; i < n; ++i) {
        if (td_add(h, 42.0, 1) != 0) {
            fprintf(stderr, "td_add failed at %d\n", i);
            exit(1);
        }
    }
    if (h->unmerged_nodes != n) {
        fprintf(stderr, "expected one big compress, but auto-compress ran (unmerged=%d)\n",
                h->unmerged_nodes);
        exit(1);
    }
    td_sort_comparisons = 0;
    if (td_compress(h) != 0) {
        fprintf(stderr, "compress failed at n=%d\n", n);
        exit(1);
    }
    const double c = (double)td_sort_comparisons;
    /* All values equal -> collapses to a single centroid, trivially sorted. */
    for (int i = 1; i < h->merged_nodes; ++i) {
        CHECK(h->nodes_mean[i - 1] <= h->nodes_mean[i], "A: centroids not sorted at %d (n=%d)", i,
              n);
    }
    td_free(h);
    return c;
}

/* Part B: run the heapsort fallback directly on reverse-sorted input; return the comparisons. */
static double compares_heapsort(int n) {
    double *m = (double *)calloc((size_t)n, sizeof(double));
    long long *w = (long long *)calloc((size_t)n, sizeof(long long));
    if (m == NULL || w == NULL) {
        fprintf(stderr, "allocation failed at n=%d\n", n);
        exit(1);
    }
    for (int i = 0; i < n; ++i) {
        m[i] = (double)(n - i); /* strictly descending */
        w[i] = 1;
    }
    td_sort_comparisons = 0;
    td_heap_sort(m, w, 0, n - 1);
    const double c = (double)td_sort_comparisons;
    for (int i = 1; i < n; ++i) {
        CHECK(m[i - 1] <= m[i], "B: heapsort output not sorted at %d (n=%d)", i, n);
    }
    free(m);
    free(w);
    return c;
}

int main(void) {
    const int sizes[] = {25000, 50000, 100000, 200000};
    const int nsizes = (int)(sizeof(sizes) / sizeof(sizes[0]));

    /* Part A: duplicate-heavy input must stay LINEAR (3-way partition). New sort = 2n; the
     * pre-PR Lomuto sort = n^2/2. Bound 8n leaves 4x margin and is far below quadratic. */
    printf("Part A - duplicate-heavy (MOD-17228 DoS), must be linear:\n");
    for (int i = 0; i < nsizes; ++i) {
        const int n = sizes[i];
        const double c = compares_all_equal(n);
        const double linear_bound = 8.0 * (double)n;
        const double quadratic = 0.5 * (double)n * (double)n;
        printf("  n=%7d  cmps=%12.0f  c/n=%5.2f  bound(8n)=%.0f  (pre-PR ~n^2/2=%.0f)\n", n, c,
               c / n, linear_bound, quadratic);
        CHECK(c <= linear_bound, "A: n=%d comparisons %.0f exceed linear bound %.0f (quadratic?)",
              n, c, linear_bound);
    }

    /* Part B: heapsort fallback must be O(n log n). Measured ~1.78 n log2 n; bound 4 n log2 n.
     * Also assert the normalized ratio stays roughly flat (a quadratic path would blow up). */
    printf("Part B - heapsort fallback, must be O(n log n):\n");
    double max_ratio = 0.0;
    double min_ratio = 1e300;
    for (int i = 0; i < nsizes; ++i) {
        const int n = sizes[i];
        const double c = compares_heapsort(n);
        const double nlogn = (double)n * log2((double)n);
        const double ratio = c / nlogn;
        if (ratio > max_ratio) {
            max_ratio = ratio;
        }
        if (ratio < min_ratio) {
            min_ratio = ratio;
        }
        printf("  n=%7d  cmps=%12.0f  c/(n*log2n)=%5.3f  bound(4*n*log2n)=%.0f\n", n, c, ratio,
               4.0 * nlogn);
        CHECK(c <= 4.0 * nlogn, "B: n=%d comparisons %.0f exceed O(n log n) bound %.0f", n, c,
              4.0 * nlogn);
    }
    /* Flatness: for O(n log n) the ratio is ~constant; a quadratic path would grow it ~n/log n.
     * Over this size range the O(n log n) ratio moves only a few percent. */
    CHECK(max_ratio <= 2.0 * min_ratio, "B: normalized comparison ratio not flat (%.3f..%.3f)",
          min_ratio, max_ratio);

    if (failures == 0) {
        printf("OK\n");
        return 0;
    }
    fprintf(stderr, "%d complexity check(s) failed\n", failures);
    return 1;
}
