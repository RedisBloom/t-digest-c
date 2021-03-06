#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "tdigest.h"

#ifndef TD_MALLOC_INCLUDE
#define TD_MALLOC_INCLUDE "td_malloc.h"
#endif

#include TD_MALLOC_INCLUDE

void bbzero(void *to, size_t count) { memset(to, 0, count); }

static inline bool is_very_small(double val) { return !(val > .000000001 || val < -.000000001); }

static inline int cap_from_compression(double compression) { return (6 * (int)(compression)) + 10; }

static bool should_td_compress(td_histogram_t *h) {
    return ((h->merged_nodes + h->unmerged_nodes) == h->cap);
}

static int next_node(td_histogram_t *h) { return h->merged_nodes + h->unmerged_nodes; }

void td_compress(td_histogram_t *h);

int td_number_centroids(td_histogram_t *h) { return next_node(h); }

////////////////////////////////////////////////////////////////////////////////
// Constructors
////////////////////////////////////////////////////////////////////////////////

static size_t td_required_buf_size(double compression) {
    return sizeof(td_histogram_t) + (cap_from_compression(compression) * sizeof(node_t));
}

int td_init(double compression, td_histogram_t **result) {
    size_t buf_size = td_required_buf_size(compression);
    td_histogram_t *histogram = (td_histogram_t *)((char *)(__td_malloc(buf_size)));
    if (!histogram) {
        return 1;
    }
    bbzero((void *)(histogram), buf_size);
    histogram->cap = (buf_size - sizeof(td_histogram_t)) / sizeof(node_t);
    histogram->compression = compression;
    td_reset(histogram);
    *result = histogram;
    return 0;
}

td_histogram_t *td_new(double compression) {
    td_histogram_t *mdigest = NULL;
    td_init(compression, &mdigest);
    return mdigest;
}

void td_free(td_histogram_t *h) { __td_free((void *)(h)); }

void td_merge(td_histogram_t *into, td_histogram_t *from) {
    td_compress(into);
    td_compress(from);
    for (int i = 0; i < from->merged_nodes; i++) {
        node_t *n = &from->nodes[i];
        td_add(into, n->mean, n->count);
    }
}

int td_centroid_count(td_histogram_t *h) { return next_node(h); }

void td_reset(td_histogram_t *h) {
    if (h == NULL) {
        return;
    }
    h->min = __DBL_MAX__;
    h->max = __DBL_MIN__;
    h->merged_nodes = 0;
    h->merged_weight = 0;
    h->unmerged_nodes = 0;
    h->unmerged_weight = 0;
    h->total_compressions = 0;
}

double td_size(td_histogram_t *h) { return h->merged_weight + h->unmerged_weight; }

double td_cdf(td_histogram_t *h, double val) {
    td_compress(h);
    if (h->merged_nodes == 0) {
        return NAN;
    }
    double k = 0;
    int i = 0;
    node_t *n = NULL;
    for (i = 0; i < h->merged_nodes; i++) {
        n = &h->nodes[i];
        if (n->mean >= val) {
            break;
        }
        k += n->count;
    }
    if (n == NULL) {
        return NAN;
    }
    if (val == n->mean) {
        // technically this needs to find all of the nodes which contain this value and sum their
        // weight
        double count_at_value = n->count;
        for (i += 1; i < h->merged_nodes && h->nodes[i].mean == n->mean; i++) {
            count_at_value += h->nodes[i].count;
        }
        return (k + (count_at_value / 2)) / h->merged_weight;
    } else if (val > n->mean) { // past the largest
        return 1;
    } else if (i == 0) {
        return 0;
    }
    // we want to figure out where along the line from the prev node to this node, the value falls
    node_t *nr = n;
    node_t *nl = n - 1;
    k -= (nl->count / 2);
    // we say that at zero we're at nl->mean
    // and at (nl->count/2 + nr->count/2) we're at nr
    double m = (nr->mean - nl->mean) / (nl->count / 2 + nr->count / 2);
    double x = (val - nl->mean) / m;
    return (k + x) / h->merged_weight;
}

double td_quantile(td_histogram_t *h, double q) {
    td_compress(h);
    if (q < 0 || q > 1 || h->merged_nodes == 0) {
        return NAN;
    }
    // if left of the first node, use the first node
    // if right of the last node, use the last node, use it
    double goal = q * h->merged_weight;
    double k = 0;
    int i = 0;
    node_t *n = NULL;
    for (i = 0; i < h->merged_nodes; i++) {
        n = &h->nodes[i];
        if (k + n->count > goal) {
            break;
        }
        k += n->count;
    }
    if (n == NULL) {
        return NAN;
    }
    double delta_k = goal - k - (n->count / 2);
    if (is_very_small(delta_k)) {
        return n->mean;
    }
    bool right = delta_k > 0;
    if ((right && ((i + 1) == h->merged_nodes)) || (!right && (i == 0))) {
        return n->mean;
    }
    node_t *nl;
    node_t *nr;
    if (right) {
        nl = n;
        nr = &h->nodes[i + 1];
        k += (n->count / 2);
    } else {
        nl = &h->nodes[i - 1];
        nr = n;
        k -= (n->count / 2);
    }
    double x = goal - k;
    // we have two points (0, nl->mean), (nr->count, nr->mean)
    // and we want x
    double m = (nr->mean - nl->mean) / (nl->count / 2 + nr->count / 2);
    return m * x + nl->mean;
}

void td_add(td_histogram_t *h, double mean, double count) {
    if (should_td_compress(h)) {
        td_compress(h);
    }
    if (mean < h->min) {
        h->min = mean;
    }
    if (mean > h->max) {
        h->max = mean;
    }
    h->nodes[next_node(h)] = (node_t){
        .mean = mean,
        .count = count,
    };
    h->unmerged_nodes++;
    h->unmerged_weight += count;
}

static int compare_nodes(const void *v1, const void *v2) {
    node_t *n1 = (node_t *)(v1);
    node_t *n2 = (node_t *)(v2);
    const double n1m = n1->mean;
    const double n2m = n2->mean;
    if (n1m < n2m) {
        return -1;
    }
    if (n1m > n2m) {
        return 1;
    }
    return 0;
}

void td_compress(td_histogram_t *h) {
    if (h->unmerged_nodes == 0) {
        return;
    }
    int N = h->merged_nodes + h->unmerged_nodes;
    qsort((void *)(h->nodes), N, sizeof(node_t), &compare_nodes);
    double total_count = h->merged_weight + h->unmerged_weight;
    double denom = 2 * MM_PI * total_count * log(total_count);
    double normalizer = h->compression / denom;
    int cur = 0;
    double count_so_far = 0;
    for (int i = 1; i < N; i++) {
        double proposed_count = h->nodes[cur].count + h->nodes[i].count;
        double z = proposed_count * normalizer;
        double q0 = count_so_far / total_count;
        double q2 = (count_so_far + proposed_count) / total_count;
        bool should_add = (z <= (q0 * (1 - q0))) && (z <= (q2 * (1 - q2)));
        // next point will fit
        // so merge into existing centroid
        if (should_add) {
            h->nodes[cur].count += h->nodes[i].count;
            double delta = h->nodes[i].mean - h->nodes[cur].mean;
            double weighted_delta = (delta * h->nodes[i].count) / h->nodes[cur].count;
            h->nodes[cur].mean += weighted_delta;
        } else {
            count_so_far += h->nodes[cur].count;
            cur++;
            h->nodes[cur] = h->nodes[i];
        }
        if (cur != i) {
            h->nodes[i] = (node_t){
                .mean = 0,
                .count = 0,
            };
        }
    }
    h->merged_nodes = cur + 1;
    h->merged_weight = total_count;
    h->unmerged_nodes = 0;
    h->unmerged_weight = 0;
    h->total_compressions++;
}

double td_min(td_histogram_t *h) { return h->min; }

double td_max(td_histogram_t *h) { return h->max; }

int td_compression(td_histogram_t *h) { return h->compression; }

// TODO:
const double *td_centroids_weight(td_histogram_t *h) { return NULL; }

// TODO:
const double *td_centroids_mean(td_histogram_t *h) { return NULL; }

double td_centroids_weight_at(td_histogram_t *h, int pos) {
    if (pos < 0 || pos > h->merged_nodes) {
        return NAN;
    }
    return h->nodes[pos].count;
}

double td_centroids_mean_at(td_histogram_t *h, int pos) {
    if (pos < 0 || pos > h->merged_nodes) {
        return NAN;
    }
    return h->nodes[pos].mean;
}
