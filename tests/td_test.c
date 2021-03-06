/**
 * td_test.c
 * Written by Filipe Oliveira and released to the public domain,
 * as explained at http://creativecommons.org/publicdomain/zero/1.0/
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>

#include <stdio.h>
#include "tdigest.h"

#include "minunit.h"

#define STREAM_SIZE 1000000

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

static double randfrom(double M, double N) { return M + (rand() / (RAND_MAX / (N - M))); }

/**
 * Reference implementations for cdf if we have all data.
 */
static double dist_cdf(double x, double *data, int data_length) {
    double n1 = 0;
    double n2 = 0;
    for (size_t i = 0; i < data_length; i++) {
        const double v = data[i];
        n1 += (v < x) ? 1 : 0;
        n2 += (v == x) ? 1 : 0;
    }
    return (n1 + n2 / 2.0) / data_length;
}

/**
 * Reference implementations for quantile if we have all data.
 */
static double dist_quantile(double q, double *data, int data_length) {
    if (data_length == 0) {
        return NAN;
    }
    double index = q * data_length;
    if (index < 0) {
        index = 0;
    }
    if (index > data_length - 1) {
        index = data_length - 1;
    }
    return data[(int)floor(index)];
}

int tests_run = 0;

td_histogram_t *histogram = NULL;

static void load_histograms() {
    const int compression = 500;

    int i;
    if (histogram) {
        td_free(histogram);
    }
    histogram = td_new(compression);

    for (i = 0; i < STREAM_SIZE; i++) {
        td_add(histogram, randfrom(0, 10), 1);
    }
}

MU_TEST(test_basic) {
    td_histogram_t *t = td_new(10);
    mu_assert(t != NULL, "created_histogram");
    mu_assert_double_eq(0, t->unmerged_weight);
    mu_assert_double_eq(0, t->merged_weight);
    td_add(t, 0.0, 1);
    // with one data point, all quantiles lead to Rome
    mu_assert_double_eq(0.0, td_quantile(t, .0));
    mu_assert_double_eq(0.0, td_quantile(t, 0.5));
    mu_assert_double_eq(0.0, td_quantile(t, 1.0));
    td_add(t, 10.0, 1);
    mu_assert_double_eq(0.0, td_min(t));
    mu_assert_double_eq(10.0, td_max(t));
    mu_assert_double_eq(2.0, td_size(t));
    mu_assert(t != NULL, "Failed to allocate hdr_histogram");
    mu_assert_double_eq(10.0, t->compression);
    mu_assert(td_compression(t) < t->cap, "False: buffer size < compression");
    mu_assert_double_eq(0.0, td_quantile(t, .0));
    mu_assert_double_eq(0.0, td_quantile(t, .1));
    mu_assert_double_eq(10.0, td_quantile(t, .99));
    td_reset(t);
    td_reset(NULL);
    td_free(t);
}

MU_TEST(test_quantile_interpolations) {
    td_histogram_t *t = td_new(10);
    mu_assert(t != NULL, "created_histogram");
    mu_assert_double_eq(0, t->unmerged_weight);
    mu_assert_double_eq(0, t->merged_weight);
    td_add(t, 5.0, 2);
    mu_assert_double_eq(1, t->unmerged_weight);
    // with one data point, all quantiles lead to Rome
    mu_assert_double_eq(0.0, td_quantile(t, .0));
    mu_assert_double_eq(0.0, td_quantile(t, 0.5));
    td_compress(t);
    mu_assert_double_eq(0, t->unmerged_weight);
    mu_assert_double_eq(2.0, t->merged_weight);
    td_add(t, 100.0, 1);
    // we know that there are at least two centroids now
    td_free(t);
}

MU_TEST(test_compress_small) {
    td_histogram_t *t = td_new(100);
    mu_assert(t != NULL, "created_histogram");
    td_add(t, 1.0, 1);
    mu_assert_double_eq(1.0, td_min(t));
    mu_assert_double_eq(1.0, td_max(t));
    mu_assert_double_eq(1.0, td_size(t));
    mu_assert_int_eq(1, td_centroid_count(t));
    mu_assert_long_eq(0, t->total_compressions);
    mu_assert_double_eq(1.0, td_centroids_mean_at(t, 0));
    mu_assert_double_eq(1.0, td_centroids_weight_at(t, 0));
    mu_assert_int_eq(1, t->unmerged_nodes);
    mu_assert_int_eq(0, t->merged_nodes);
    td_compress(t);
    mu_assert_int_eq(0, t->unmerged_nodes);
    mu_assert_int_eq(1, t->merged_nodes);
    mu_assert_long_eq(1, t->total_compressions);
    mu_assert_double_eq(1.0, td_centroids_mean_at(t, 0));
    mu_assert_double_eq(1.0, td_centroids_weight_at(t, 0));
    mu_assert_double_eq(1.0, td_quantile(t, 0.001));
    mu_assert_double_eq(1.0, td_quantile(t, 0.01));
    mu_assert_double_eq(1.0, td_quantile(t, 0.5));
    mu_assert_double_eq(1.0, td_quantile(t, 0.99));
    mu_assert_double_eq(1.0, td_quantile(t, 0.999));
    td_free(t);
}

MU_TEST(test_compress_large) {
    td_histogram_t *t = td_new(100);
    mu_assert(t != NULL, "created_histogram");
    for (int i = 1; i <= 1000; ++i) {
        td_add(t, (double)i, 1);
    }

    mu_assert_double_eq(1.0, td_min(t));
    mu_assert_double_eq(1000.0, td_max(t));
    mu_assert_double_eq(1000.0, td_size(t));
    // TODO: add this test cases
    // EXPECT_EQ(500500, digest.sum());
    // EXPECT_EQ(500.5, digest.mean());
    // mu_assert_double_eq(1.5, td_quantile(t, 0.001));
    mu_assert_double_eq(10.5, td_quantile(t, 0.01));
    // mu_assert_double_eq_epsilon(500.25, td_quantile(t, 0.5), 0.5);
    // TODO: swap this one by the bellow
    // mu_assert_double_eq(990.25, td_quantile(t, 0.99));
    mu_assert_double_eq_epsilon(990.25, td_quantile(t, 0.99), 0.5);
    // mu_assert_double_eq(999.5, td_quantile(t, 0.999));
    td_free(t);
}

MU_TEST(test_negative_values) {
    td_histogram_t *t = td_new(1000);
    mu_assert(t != NULL, "created_histogram");
    for (int i = 1; i <= 100; ++i) {
        td_add(t, (double)i, 1);
        td_add(t, -(double)i, 1);
    }
    mu_assert_double_eq(-100.0, td_min(t));
    mu_assert_double_eq(100.0, td_max(t));
    mu_assert_double_eq(200.0, td_size(t));
    mu_assert_double_eq(-100, td_quantile(t, 0.0));
    mu_assert_double_eq(-100, td_quantile(t, 0.001));
    // TODO: fix my epsilon
    mu_assert_double_eq_epsilon(-98.5, td_quantile(t, 0.01), 0.75);
    mu_assert_double_eq_epsilon(98.5, td_quantile(t, 0.99), 0.75);
    mu_assert_double_eq(100, td_quantile(t, 0.999));
    mu_assert_double_eq(100, td_quantile(t, 1.0));
    td_free(t);
}

MU_TEST(test_negative_values_merge) {
    td_histogram_t *d1 = td_new(100);
    td_histogram_t *d2 = td_new(100);
    mu_assert(d1 != NULL, "created_histogram");
    mu_assert(d2 != NULL, "created_histogram");
    for (int i = 1; i <= 100; ++i) {
        td_add(d1, (double)i, 1);
        td_add(d2, -(double)i, 1);
    }
    td_merge(d1, d2);
    mu_assert_double_eq(-100.0, td_min(d1));
    mu_assert_double_eq(100.0, td_max(d1));
    mu_assert_double_eq(200.0, td_size(d1));
    mu_assert_double_eq(-100, td_quantile(d1, 0.0));
    mu_assert_double_eq(-100, td_quantile(d1, 0.001));
    // TODO: fix my epsilon
    mu_assert_double_eq_epsilon(-98.5, td_quantile(d1, 0.01), 0.75);
    mu_assert_double_eq_epsilon(98.5, td_quantile(d1, 0.99), 0.75);
    mu_assert_double_eq(100, td_quantile(d1, 0.999));
    mu_assert_double_eq(100, td_quantile(d1, 1.0));
    td_free(d1);
    td_free(d2);
}

MU_TEST(test_large_outlier_test) {
    td_histogram_t *t = td_new(100);
    mu_assert(t != NULL, "created_histogram");
    for (int i = 1; i <= 19; ++i) {
        td_add(t, (double)i, 1);
    }
    td_add(t, 1000000, 1);
    mu_assert(td_quantile(t, 0.5) < td_quantile(t, 0.9),
              "False: td_quantile(t, 0.5) < td_quantile(t, 0.9)");
    td_free(t);
}

MU_TEST(test_nans) {
    td_histogram_t *t = td_new(1000);
    mu_assert(isnan(td_quantile(t, 0)), "empty value at 0");
    mu_assert(isnan(td_quantile(t, 0.5)), "empty value at .5");
    mu_assert(isnan(td_quantile(t, 1)), "empty value at 1");
    mu_assert(isnan(td_centroids_weight_at(t, 1)),
              "td_centroids_weight_at on pos > h->merged_nodes");
    mu_assert(isnan(td_centroids_weight_at(t, -1)), "td_centroids_weight_at on pos < 0");
    mu_assert(isnan(td_centroids_mean_at(t, 1)), "td_centroids_mean_at on pos > h->merged_nodes");
    mu_assert(isnan(td_centroids_mean_at(t, -1)), "td_centroids_mean_at on pos < 0");
    td_add(t, 1, 1);
    mu_assert(isnan(td_quantile(t, -.1)), "value at -0.1");
    mu_assert(isnan(td_quantile(t, 1.1)), "value at 1.1");
    td_free(t);
}

MU_TEST(test_two_interp) {
    td_histogram_t *t = td_new(1000);
    td_add(t, 1, 1);
    td_add(t, 10, 1);
    mu_assert(isfinite(td_quantile(t, .9)), "test_two_interp: value at .9");
    td_reset(t);
    // if the left centroid has more than one sample, we still know
    // that one sample occurred at min so we can do some interpolation
    td_add(t, 1, 10);
    td_add(t, 10, 1);
    mu_assert_double_eq(1.0, td_quantile(t, .1));
    td_reset(t);
    // if the right-most centroid has more than one sample, we still know
    // that one sample occurred at max so we can do some interpolation
    td_add(t, 1, 1);
    td_add(t, 10, 10);
    mu_assert_double_eq(10.0, td_quantile(t, .9));
    td_reset(t);
    // in between extremes we interpolate between centroids
    td_add(t, 1, 1);
    td_add(t, 5, 1);
    td_add(t, 10, 1);
    // centroids i and i+1 bracket our current point
    // check for unit weight
    // within the singleton's sphere
    // left
    mu_assert_double_eq(5.0, td_quantile(t, .5));
    td_reset(t);
    // in between extremes we interpolate between centroids
    td_add(t, 1, 1);  // q0
    td_add(t, 4, 1);  // q20
    td_add(t, 8, 1);  // q40
    td_add(t, 12, 1); // q60
    td_add(t, 16, 1); // q80
    td_add(t, 20, 1); // q100
    // centroids i and i+1 bracket our current point
    // check for unit weight
    // within the singleton's sphere
    // TODO: check for right
    // mu_assert_double_eq(4.0, td_quantile(t, .20) );
    // mu_assert_double_eq(8.0, td_quantile(t, .40) );
    // mu_assert_double_eq(12.0, td_quantile(t, .60) );
    // mu_assert_double_eq(7.0, td_quantile(t, .70) );
    // mu_assert_double_eq(8.0, td_quantile(t, .75) );
    td_free(t);
}

MU_TEST(test_cdf) {
    td_histogram_t *t = td_new(100);
    mu_assert(isnan(td_cdf(t, 1.1)), "no data to examine");
    // interpolate if somehow we have weight > 0 and max != min
    td_add(t, 1, 1);
    // bellow lower bound
    mu_assert_double_eq(0, td_cdf(t, 0));
    // exactly one centroid, should have max==min
    // min and max are too close together to do any viable interpolation
    mu_assert_double_eq(0.5, td_cdf(t, 1));
    // above upper bound
    mu_assert_double_eq(1.0, td_cdf(t, 2));
    td_add(t, 10, 1);
    mu_assert_double_eq(.25, td_cdf(t, 1));
    mu_assert_double_eq(.5, td_cdf(t, 5.5));
    // // TODO: fix this
    // mu_assert_double_eq(1,td_cdf(t, 10));
    td_free(t);
}

MU_TEST(test_td_size) {
    load_histograms();
    mu_assert(td_size(histogram) == STREAM_SIZE, "td_size(histogram) != STREAM_SIZE");
}

MU_TEST(test_td_max) {
    load_histograms();
    mu_assert_double_eq_epsilon(10.0, td_max(histogram), 0.001);
}

MU_TEST(test_td_min) {
    load_histograms();
    mu_assert_double_eq_epsilon(0.0, td_min(histogram), 0.001);
}

MU_TEST(test_quantiles) {
    load_histograms();
    mu_assert_double_eq_epsilon(0.0, td_quantile(histogram, 0.0), 0.001);
    mu_assert_double_eq_epsilon(1.0, td_quantile(histogram, 0.1), 0.02);
    mu_assert_double_eq_epsilon(2.0, td_quantile(histogram, 0.2), 0.02);
    mu_assert_double_eq_epsilon(3.0, td_quantile(histogram, 0.3), 0.03);
    mu_assert_double_eq_epsilon(4.0, td_quantile(histogram, 0.4), 0.04);
    mu_assert_double_eq_epsilon(5.0, td_quantile(histogram, 0.5), 0.05);
    mu_assert_double_eq_epsilon(6.0, td_quantile(histogram, 0.6), 0.04);
    mu_assert_double_eq_epsilon(7.0, td_quantile(histogram, 0.7), 0.03);
    mu_assert_double_eq_epsilon(8.0, td_quantile(histogram, 0.8), 0.02);
    mu_assert_double_eq_epsilon(9.0, td_quantile(histogram, 0.9), 0.02);
    mu_assert_double_eq_epsilon(9.99, td_quantile(histogram, 0.999), 0.01);
    mu_assert_double_eq_epsilon(9.999, td_quantile(histogram, 0.9999), 0.01);
    mu_assert_double_eq_epsilon(9.9999, td_quantile(histogram, 0.99999), 0.01);
    mu_assert_double_eq_epsilon(10.0, td_quantile(histogram, 1.0), 0.001);
}

MU_TEST_SUITE(test_suite) {
    MU_RUN_TEST(test_basic);
    MU_RUN_TEST(test_compress_small);
    MU_RUN_TEST(test_compress_large);
    MU_RUN_TEST(test_nans);
    MU_RUN_TEST(test_negative_values);
    MU_RUN_TEST(test_negative_values_merge);
    MU_RUN_TEST(test_large_outlier_test);
    MU_RUN_TEST(test_two_interp);
    MU_RUN_TEST(test_cdf);
    MU_RUN_TEST(test_td_size);
    MU_RUN_TEST(test_td_max);
    MU_RUN_TEST(test_td_min);
    MU_RUN_TEST(test_quantiles);
}

int main(int argc, char *argv[]) {
    MU_RUN_SUITE(test_suite);
    MU_REPORT();
    return MU_EXIT_CODE;
}
