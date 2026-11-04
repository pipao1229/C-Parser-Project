#define SWAP(a, b) { int t_ = a; \
                    a = b; \
                    b = t_; }
int main(void) { int x = 1, y = 2; SWAP(x, y); return x; }