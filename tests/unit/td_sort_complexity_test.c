/*
 * Complexity regression for the centroid sort (MOD-17228).
 *
 * A correctness-only test cannot distinguish an O(n log n) sort from an O(n^2)
 * one, and the ordinary unit test auto-compresses in small batches so it never
 * exercises one large partition. This test compiles the library with
 * TD_INSTRUMENT_SORT (which exposes a key-comparison counter), forces a SINGLE
 * compress over a large adversarial permutation, and asserts the comparison
 * count stays within an O(n log n) bound.
 *
 * The input is a DISTINCT-value "midpoint killer": values are assigned so that the
 * midpoint of every recursion range is the minimum of that range. A deterministic
 * midpoint-pivot sort therefore peels one element per level -> ~n^2/2 comparisons
 * (this is exactly the distinct-permutation weakness of a fixed-position pivot).
 * Introsort's median-of-three pivot plus heapsort fallback keeps it O(n log n).
 */
#define TD_INSTRUMENT_SORT
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "tdigest.c" /* brings in the static sort + td_sort_comparisons */

/* Assign strictly increasing values to midpoints in recursion order, so the
 * midpoint of any subrange is that subrange's minimum -> worst case for a
 * midpoint-pivot quicksort, with all values distinct. */
static void fill_midpoint_killer(double *a, int lo, int hi, int *next) {
    if (lo > hi) {
        return;
    }
    const int mid = lo + (hi - lo) / 2;
    a[mid] = (double)(*next)++;
    fill_midpoint_killer(a, lo, mid - 1, next);
    fill_midpoint_killer(a, mid + 1, hi, next);
}

int main(void) {
    const int n = 100000;

    double *killer = (double *)malloc((size_t)n * sizeof(double));
    if (killer == NULL) {
        fprintf(stderr, "allocation failed\n");
        return 1;
    }
    int next = 0;
    fill_midpoint_killer(killer, 0, n - 1, &next);

    /* Compression large enough that cap = 6*compression+10 > n, so all n points
     * buffer and are sorted by a single td_compress() -- one big partition. */
    td_histogram_t *h = td_new(n);
    if (h == NULL) {
        fprintf(stderr, "allocation failed\n");
        free(killer);
        return 1;
    }
    for (int i = 0; i < n; ++i) {
        if (td_add(h, killer[i], 1) != 0) {
            fprintf(stderr, "td_add failed at %d\n", i);
            free(killer);
            return 1;
        }
    }
    free(killer);
    if (h->unmerged_nodes != n) {
        fprintf(stderr, "expected one big compress, but auto-compress ran (unmerged=%d)\n",
                h->unmerged_nodes);
        return 1;
    }

    td_sort_comparisons = 0;
    if (td_compress(h) != 0) {
        fprintf(stderr, "compress failed\n");
        return 1;
    }

    const double bound = 50.0 * (double)n * log2((double)n); /* generous O(n log n) */
    const double quadratic = 0.5 * (double)n * (double)n;    /* what the old sort would need */
    printf("n=%d  one-compress key-comparisons=%llu  O(n log n) bound=%.0f  (quadratic ~%.0f)\n", n,
           td_sort_comparisons, bound, quadratic);

    int rc = 0;
    if ((double)td_sort_comparisons > bound) {
        fprintf(stderr,
                "FAIL: comparison count exceeds the O(n log n) bound -- sort is quadratic\n");
        rc = 1;
    }
    /* sanity: the digest is actually sorted/usable after the compress */
    for (int i = 1; i < h->merged_nodes; ++i) {
        if (h->nodes_mean[i - 1] > h->nodes_mean[i]) {
            fprintf(stderr, "FAIL: centroids not sorted at %d\n", i);
            rc = 1;
            break;
        }
    }
    td_free(h);
    if (rc == 0) {
        printf("OK\n");
    }
    return rc;
}
