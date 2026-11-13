#ifndef FRAME_CLASSES_H
#define FRAME_CLASSES_H
#include "frame_entity_factory.h"
/*4F4270/4150/42B0/43B0 and allocation/string/hash helpers. Names are logical
 * DATA strings. ASCII names only; high bytes return-5, budget-3.
 * Intern on miss creates the real36-byte tagged symbol and hash-chain entry. */
int frame_symbol_intern(frame_data_read read,frame_data_write write,void *ctx,uint32_t name,
                             const frame_entity_factory_services *entities,uint32_t *out);
/* Same rules for a bounded native token, including its terminating NUL. No
 * synthetic DATA stack or host pointer is installed in game memory. */
int frame_symbol_intern_name(frame_data_read read,frame_data_write write,void *ctx,const unsigned char *name,uint32_t bytes,
                                  const frame_entity_factory_services *entities,uint32_t *out);
/*4F4350 default tagged-value record, used by interface declarations. */
int frame_symbol_create(frame_data_read read,frame_data_write write,void *ctx,
                             const frame_entity_factory_services *entities,uint32_t *out);
/* Full42C580: create all50 interface records in original order. */
int frame_interfaces_initialize(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *entities);
/* Full4F4400: clear131 buckets, create self-referential nil/default value and
 * insert its real name. Static string534D00 and bucket pointer are inputs. */
int frame_symbols_initialize(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *entities);
/* Full5049A0: capacity rounds to25, copy old table then zero tail/free old. */
int frame_class_grow(frame_data_read read,frame_data_write write,void *ctx,uint32_t klass,int32_t capacity,
                          const frame_entity_factory_services *entities);
/* Full504890. class_bytes(ECX) is descriptor allocation size; object_bytes is
 * stack arg1, NOT a numeric class ID. Parent descriptor bytes copied first. */
int frame_class_create(frame_data_read read,frame_data_write write,void *ctx,uint32_t class_bytes,uint32_t name,uint32_t object_bytes,uint32_t parent,
                            const frame_entity_factory_services *entities,uint32_t *out);
/* Full504A50 interface slot assignment, arg-count table and method binding. */
int frame_class_bind(frame_data_read read,frame_data_write write,void *ctx,uint32_t klass,uint32_t interface,uint32_t arguments,uint32_t method,
                          const frame_entity_factory_services *entities);
/* Full504A30; unknown name still interns a symbol, returns out0. */
int frame_class_lookup(frame_data_read read,frame_data_write write,void *ctx,uint32_t name,
                            const frame_entity_factory_services *entities,uint32_t *out);
int frame_class_lookup_name(frame_data_read read,frame_data_write write,void *ctx,const unsigned char *name,uint32_t bytes,
                                 const frame_entity_factory_services *entities,uint32_t *out);
#endif
