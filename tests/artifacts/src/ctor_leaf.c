int g_sequence = 0;
int g_leaf_seq = 0;

__attribute__((constructor))
void leaf_ctor(void) {
    g_leaf_seq = ++g_sequence;
}

int get_leaf_seq(void) {
    return g_leaf_seq;
}
