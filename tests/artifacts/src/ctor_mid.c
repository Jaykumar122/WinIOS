extern int g_sequence;
int g_mid_seq = 0;

__attribute__((constructor))
void mid_ctor(void) {
    g_mid_seq = ++g_sequence;
}

int get_mid_seq(void) {
    return g_mid_seq;
}
