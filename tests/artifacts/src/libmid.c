extern int leaf_mul(int a, int b);

int mid_compute(int a, int b) {
    int m = leaf_mul(a, b);
    return m + 100;
}
