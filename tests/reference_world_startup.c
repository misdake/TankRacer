/* Complete construction oracle: real startup, per-record RNG and owned data.
 * The linker wrapper observes unchanged production C; it never replaces a call. */
#include "reference_game.h"
#include "frame_native.h"

typedef struct record { uint32_t entity, before, after, kind; } record;
static record records[4096];
static uint32_t count, active = UINT32_MAX;
static uint32_t word(uint32_t a) { return reference_game_word(a, 4); }
static uint32_t narrow(uint32_t a, unsigned n) { return reference_game_word(a, n); }
static uint32_t object_of(uint32_t id)
{
    uint32_t r = word(word(0x55c694) + id * 8u);
    return r ? word(r + 4u) : 0;
}
int __real_frame_native_dispatch(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *);
int __wrap_frame_native_dispatch(void *ctx, uint32_t target, uint32_t entity,
                                 uint32_t has, uint32_t payload, uint32_t *out)
{
    uint32_t saved = active, index = UINT32_MAX, kind = 0;
    int status;
    if (target == 0x504020u && has) {
        if (count >= 4096) return -3;
        index = count++;
        active = index;
        records[index] = (record){entity, word(0x55b6ac), 0, 0};
    }
    if (active != UINT32_MAX && records[active].entity == entity) {
        switch (target) {
        case 0x41b740: kind = 1; break; /* Noisy */
        case 0x42a440: kind = 2; break; /* Smoker */
        case 0x43d2a0: kind = 3; break; /* Trackcam */
        case 0x404720: kind = 4; break; /* Bomber */
        case 0x43e360: kind = 5; break; /* Train */
        case 0x40b250: kind = 6; break; /* Flock */
        case 0x4227d0: kind = 7; break; /* Powerup */
        case 0x404040: kind = 8; break; /* BckgrdOb */
        case 0x413520: kind = 9; break; /* Lamps */
        default: break;
        }
        if (kind) records[active].kind = kind;
    }
    status = __real_frame_native_dispatch(ctx, target, entity, has, payload, out);
    if (index != UINT32_MAX) {
        records[index].after = word(0x55b6ac);
        active = saved;
    }
    return status;
}
static void words(uint32_t a, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) oracle_put(word(a + i * 4u));
}
static void axis(uint32_t p)
{
    unsigned i;
    for (i = 0; i < 8; i += 2) oracle_put(narrow(p + i, 2));
    oracle_put(narrow(p + 8, 1));
    oracle_put(narrow(p + 9, 1));
}
static void actor(uint32_t o, uint32_t kind)
{
    uint32_t i, j, p;
    switch (kind) {
    case 1: /* Noisy: selected sample and numeric sound parameters. */
        oracle_put(word(o + 0x7c)); oracle_put(word(o + 0x78));
        for (i = 0; i < word(o + 0x78); i++) words(0x544e18 + i * 20, 4);
        break;
    case 2: /* Smoker: flags and all initial particles. */
        oracle_put(narrow(o + 0x31e, 4)); oracle_put(narrow(o + 0x31d, 1));
        for (i = 0; i < 16; i++) {
            oracle_put(narrow(o + 0x90 + i * 42, 2));
            words(o + 0x92 + i * 42, 3);
        }
        break;
    case 3: /* Trackcam: displacement, mode, oscillators and rotations. */
        words(o + 0xb8, 3); oracle_put(word(o + 0xec)); oracle_put(word(o + 0x126));
        words(o + 0x132, 3); words(o + 0x9c, 3);
        oracle_put(narrow(o + 0x104, 2)); oracle_put(narrow(o + 0x12e, 2));
        axis(o + 0xfa); axis(o + 0xf0);
        oracle_put(narrow(o + 0x10a, 2)); oracle_put(narrow(o + 0x10c, 2));
        oracle_put(narrow(o + 0x10e, 1)); oracle_put(narrow(o + 0x10f, 1));
        axis(o + 0x110);
        oracle_put(narrow(o + 0x12a, 2)); oracle_put(narrow(o + 0x12c, 2));
        break;
    case 4: /* Bomber: actual random timer, trigger, model and effect style. */
        words(o + 0x7c, 2); words(o + 0x84, 3); words(o + 0x90, 2);
        words(o + 0x98, 4); oracle_put(word(o + 0xa8));
        for (i = 0; i < 4; i++) oracle_put(narrow(o + 0xb0 + i, 1));
        oracle_put(word(o + 0xb4) != 0); oracle_put(word(o + 0xb8));
        words(o + 0xbc, 3); oracle_put(word(o + 0xc8));
        break;
    case 5: /* Train: motion branch, propeller, yacht rates and Ferris basis. */
        oracle_put(word(o + 0xac)); oracle_put(word(o + 0x8c));
        oracle_put(word(o + 0xb4)); words(o + 0x94, 6); words(o + 0x50, 3);
        oracle_put(narrow(o + 0xf4, 2)); oracle_put(narrow(o + 0xf6, 2));
        words(o + 0x128, 3); words(o + 0x14c, 1); words(o + 0x154, 1);
        words(o + 0x158, 3);
        if (word(o + 0x84) == 0x43f0e0) words(o + 0xc4, 9);
        break;
    case 6: /* Flock: all birds plus cloned-model radius and extreme vertices. */
        oracle_put(word(o + 0x334)); oracle_put(word(o + 0x32c));
        oracle_put(narrow(o + 0x330, 2));
        for (i = 0; i < 8; i++) {
            p = o + 0x80 + i * 80;
            words(p - 4, 3); words(p + 28, 9);
            oracle_put(word(p + 20)); oracle_put(word(p + 24));
        }
        j = word(o + 0x334) == 3 ? 1 : 2;
        oracle_put(j);
        for (i = 0; i < j; i++) {
            p = word(o + (i ? 0x324 : 72));
            oracle_put(narrow(p + 12, 2)); oracle_put(narrow(p + 14, 2));
        }
        oracle_put(j == 1 ? 0 : 2);
        if (j == 2) for (i = 0; i < 2; i++) {
            p = word(word(o + (i ? 0x324 : 72)) + 4);
            oracle_put((word(o + 0x314 + i * 8) - p) / 8);
            oracle_put((word(o + 0x318 + i * 8) - p) / 8);
        }
        break;
    case 7: /* Powerup: available type, ground, fall range and lap visibility. */
        oracle_put(word(o + 0xae)); oracle_put(word(o + 0x14f));
        oracle_put(word(o + 0xf6)); oracle_put(narrow(o + 0x7a, 2));
        oracle_put(narrow(o + 0x78, 2)); oracle_put(word(o + 0xb6));
        break;
    case 8: /* Background: linked target is normalized to an entity ID. */
        oracle_put(word(o + 0x98)); oracle_put(word(o + 0x7c)); words(o + 0x88, 3);
        oracle_put(narrow(o + 0x9c, 2)); oracle_put(narrow(o + 0x9e, 2));
        oracle_put(word(o + 0x84));
        break;
    case 9: /* Lamps: every published table entry. */
        oracle_put(word(o + 0x78));
        for (i = 0; i < word(o + 0x78); i++) {
            p = 0x53e578 + i * 24;
            words(p, 3);
            for (j = 12; j < 18; j++) oracle_put(narrow(p + j, 1));
            for (j = 18; j < 24; j += 2) oracle_put(narrow(p + j, 2));
        }
        break;
    default: break;
    }
}
static void list(uint32_t address)
{
    uint32_t head = word(address), p = word(head), n = 0;
    while (p != head) {
        if (n++ > 100000) exit(8);
        p = word(p);
    }
    oracle_put(n);
    for (p = word(head); p != head; p = word(p)) oracle_put(word(p + 8));
}
int main(int argc, char **argv)
{
    uint32_t map, cup, car, i, o, player, camera, finish;
    int status;
    if (argc != 5) return 2;
    map = (uint32_t)strtoul(argv[2], 0, 10);
    cup = (uint32_t)strtoul(argv[3], 0, 10);
    car = (uint32_t)strtoul(argv[4], 0, 10);
    oracle_binary();
    status = reference_game_start_cup(argv[1], map, cup, car, 0, 0);
    if (status) { fprintf(stderr, "startup %d\n", status); return 7; }
    player = word(0x53e8b8); camera = word(0x53cc38); finish = word(0x53b69c);
    fwrite("WS01", 1, 4, stdout);
    oracle_put(word(0x55b6ac)); oracle_put(player); oracle_put(camera);
    oracle_put(finish); oracle_put(count);
    for (i = 0; i < count; i++) {
        record r = records[i];
        o = object_of(r.entity);
        oracle_put(r.entity); oracle_put(r.before); oracle_put(r.after); oracle_put(r.kind);
        words(o, 3); words(o + 24, 9); actor(o, r.kind);
    }
    o = object_of(camera);
    words(o, 3); words(o + 0x9c, 3); words(o + 0xb8, 3); words(o + 0xec, 3);
    for (i = 0; i < 8; i++) oracle_put(narrow(o + 0xf8 + i, 1));
    o = object_of(finish);
    oracle_put(narrow(o + 0xde6, 1)); oracle_put(word(o + 0x10f3));
    oracle_put((word(o + 0x10f7) - 0x53b6a0) / 1820);
    oracle_put(narrow(o + 0xde5, 1)); words(o + 0x60, 6);
    oracle_put(narrow(object_of(player) + 0x10ba, 2));
    oracle_put(word(0x54509c)); oracle_put(word(0x5450a4));
    list(0x55b6a8); list(0x55b6bc); list(0x55b6b8); list(0x54b488);
    list(0x54b48c); list(0x53cd18); list(0x53e4b4);
    for (i = 1; i <= finish; i++) oracle_put(word(object_of(i) + 16) & 56u);
    tank_destroy();
    return 0;
}
