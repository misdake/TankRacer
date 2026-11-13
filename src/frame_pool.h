#ifndef FRAME_POOL_H
#define FRAME_POOL_H
#include "frame_data.h"
/* Standard-library memory service: accepts only allocations owned by the host.
 * Game free-list/merging/list rules below stay in C, not in this callback. */
typedef int (*frame_pool_system_free)(void *ctx,uint32_t pointer);
/* 0 with out=0 is ordinary allocation failure; negative is a host fault. */
typedef int (*frame_pool_system_alloc)(void *ctx,uint32_t bytes,uint32_t *out);
/* Complete game allocator 4F4840 and chunk/reserve path 4F4990. Standard
 * malloc/free are external; cached classes, bisection fallback, reserves,
 * compaction, budget and statistics are C rules. Work limit is explicit. */
int frame_pool_allocate(frame_data_read read,frame_data_write write,void *ctx,uint32_t bytes,
                          frame_pool_system_alloc allocate,frame_pool_system_free release,
                          void *system_ctx,uint32_t work_limit,uint32_t *out);
/* Full4F4C80: game allocation, then forward DWORD/byte zeroing (not calloc).
 * Nonempty null allocation faults before the first zero store, returning-4. */
int frame_pool_allocate_zero(frame_data_read read,frame_data_write write,void *ctx,uint32_t bytes,
                               frame_pool_system_alloc allocate,frame_pool_system_free release,
                               void *system_ctx,uint32_t work_limit,uint32_t *out);
int frame_pool_reserve_allocate(frame_data_read read,frame_data_write write,void *ctx,uint32_t bytes,
                                  frame_pool_system_alloc allocate,frame_pool_system_free release,
                                  void *system_ctx,uint32_t work_limit,uint32_t *out);
/* Full 4F4CB0. size0 no-op, four-byte rounding/wrap, cache or external free. */
int frame_pool_release(frame_data_read read,frame_data_write write,void *ctx,uint32_t pointer,uint32_t bytes,
                         frame_pool_system_free release,void *release_ctx);
/* Full 4F4C10 and 4F46F0; work limit guards malformed cyclic free lists.
 * -3 incomplete, -1 unavailable DATA. Output only on success. */
int frame_pool_split(frame_data_read read,frame_data_write write,void *ctx,int32_t bytes,
                       uint32_t work_limit,uint32_t *out);
int frame_pool_merge_adjacent(frame_data_read read,frame_data_write write,void *ctx,int32_t bytes,
                                uint32_t pointer,int32_t other_class,uint32_t work_limit,uint32_t *out);
/* Full 4F4790/4F4810 compact one/all classes; full 4F46B0 reserve registration. */
int frame_pool_compact(frame_data_read read,frame_data_write write,void *ctx,int32_t size_class,uint32_t work_limit);
int frame_pool_compact_all(frame_data_read read,frame_data_write write,void *ctx,uint32_t work_limit);
int frame_pool_add_reserve(frame_data_read read,frame_data_write write,void *ctx,uint32_t pointer,uint32_t bytes);
/* Full 4F3FD0 unlink+recycle, 4F3F20 recycle-only and 4F4020 pop-front.
 * These use game pool release, never a guessed allocator or original code. */
int frame_list_remove(frame_data_read read,frame_data_write write,void *ctx,uint32_t node,
                        frame_pool_system_free release,void *release_ctx);
int frame_list_recycle(frame_data_read read,frame_data_write write,void *ctx,uint32_t node,
                         frame_pool_system_free release,void *release_ctx);
int frame_list_pop(frame_data_read read,frame_data_write write,void *ctx,uint32_t head,
                     frame_pool_system_free release,void *release_ctx,uint32_t *out);
/* Full 4F3EF0 and 4F3FA0. Node count increments before allocation even on OOM.
 * Append's output names the allocated node (0 on OOM), not a guest register. */
int frame_list_new(frame_data_read read,frame_data_write write,void *ctx,
                     frame_pool_system_alloc allocate,frame_pool_system_free release,
                     void *system_ctx,uint32_t work_limit,uint32_t *out);
/* Full4F3F50 owning circular sentinel; allocation failure returns out=0. */
int frame_list_create(frame_data_read read,frame_data_write write,void *ctx,
                        frame_pool_system_alloc allocate,frame_pool_system_free release,
                        void *system_ctx,uint32_t work_limit,uint32_t *out);
int frame_list_append(frame_data_read read,frame_data_write write,void *ctx,uint32_t value,uint32_t head,
                        frame_pool_system_alloc allocate,frame_pool_system_free release,
                        void *system_ctx,uint32_t work_limit,uint32_t *out);
/* Full 4F3F70 push-front and 4F4000 remove-first-value. The latter also reads
 * sentinel.value while searching, as original, but never removes the sentinel. */
int frame_list_prepend(frame_data_read read,frame_data_write write,void *ctx,uint32_t value,uint32_t head,
                         frame_pool_system_alloc allocate,frame_pool_system_free release,
                         void *system_ctx,uint32_t work_limit,uint32_t *out);
int frame_list_remove_value(frame_data_read read,frame_data_write write,void *ctx,uint32_t value,uint32_t head,
                              frame_pool_system_free release,void *release_ctx,uint32_t work_limit);
/* Full 4F4040 / 4F4070 / 4F4090. Lookup reads sentinel.value; unique insertion
 * is prepend. Clear runs only if sentinel.owner(+8)==sentinel (original gate). */
int frame_list_contains(frame_data_read read,void *ctx,uint32_t value,uint32_t head,uint32_t work_limit,uint32_t *out);
int frame_list_prepend_unique(frame_data_read read,frame_data_write write,void *ctx,uint32_t value,uint32_t head,
                                frame_pool_system_alloc allocate,frame_pool_system_free release,
                                void *system_ctx,uint32_t work_limit);
int frame_list_clear(frame_data_read read,frame_data_write write,void *ctx,uint32_t head,
                       frame_pool_system_free release,void *release_ctx,uint32_t work_limit);
/* Full4F40B0: clear owned list, then recycle the sentinel itself. */
int frame_list_destroy(frame_data_read read,frame_data_write write,void *ctx,uint32_t head,
                         frame_pool_system_free release,void *release_ctx,uint32_t work_limit);
#endif
