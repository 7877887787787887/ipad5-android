/* SPDX-License-Identifier: MIT
 * Host-side UI/policy prototype. NOT a bootloader. No boot operations.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
enum choice { IPADOS, ANDROID };
struct readiness { bool board, signature, kernel, storage, boot_backend, recovery; };
static enum choice select_os(const struct readiness *r, bool request_android) {
    return request_android && r->board && r->signature && r->kernel &&
        r->storage && r->boot_backend && r->recovery ? ANDROID : IPADOS;
}
int main(int argc, char **argv) {
    if (argc==2 && strcmp(argv[1],"--self-test")==0) {
        for(unsigned mask=0;mask<64;mask++) {
            struct readiness r={!!(mask&1),!!(mask&2),!!(mask&4),!!(mask&8),!!(mask&16),!!(mask&32)};
            assert(select_os(&r,false)==IPADOS);
            assert(select_os(&r,true)==(mask==63?ANDROID:IPADOS));
        }
        puts("128 simulated decisions passed. Not a boot test."); return 0;
    }
    struct readiness r={0};
    puts("J72s dual-boot menu prototype");
    puts("1. iPadOS (default)\n2. Android (unavailable: independent boot backend not implemented)");
    printf("Selected policy: %s\n",select_os(&r,false)==IPADOS?"iPadOS":"Android");
    return 0;
}
