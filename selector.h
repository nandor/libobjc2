#ifndef OBJC_SELECTOR_H_INCLUDED
#define OBJC_SELECTOR_H_INCLUDED

#include <assert.h>
#include <stdlib.h>
#include <stdatomic.h>
#include "sarray2.h"

/**
 * Structure used to store the types for a selector.  This allows for a quick
 * test to see whether a selector is polymorphic and allows enumeration of all
 * type encodings for a given selector.
 *
 * This is the same size as an objc_selector, so we can allocate them from the
 * objc_selector pool.
 *
 * Note: For ABI v10, we can probably do something a bit more sensible here and
 * make selectors into a linked list.
 */
struct sel_type_list
{
  const char *value;
  struct sel_type_list *next;
};

/**
 * Selector cache entry.
 */
struct sel_entry
{
  Class class;
  IMP   imp;
  uint64_t version;
};

/**
 * Selector dispatch table.
 */
struct sel_dtable
{
#if INV_DTABLE_SIZE != 0
  struct sel_entry entries[INV_DTABLE_SIZE];
  atomic_bool lock;
  uint8_t  next;
#endif
  uint32_t size;
  uint32_t capacity;
  uint32_t index;
  BOOL is_sparse;
  union
  {
    struct objc_slot **slots;
    SparseArray *array;
  };
  struct sel_type_list type_list;
};

static inline BOOL spin_trylock(atomic_bool *lock)
{
  return atomic_exchange_explicit(lock, YES, memory_order_acq_rel) == NO;
}

static inline void spin_lock(atomic_bool *lock)
{
  do {
    while (atomic_load_explicit(lock, memory_order_relaxed));
  } while (atomic_exchange_explicit(lock, YES, memory_order_acq_rel) == YES);
}

static inline void spin_unlock(atomic_bool *lock)
{
  atomic_store_explicit(lock, NO, memory_order_release);
}

/**
 * Unregistered selector.
 */
struct objc_unreg_selector
{
  /**
   * The name of this selector.
   */
  const char *name;
  /**
   * The Objective-C type encoding of the message identified by this selector.
   */
  const char * types;
};

/**
 * Structure used to store selectors in the list.
 */
struct objc_selector
{
  union
  {
    /**
     * The name of this selector.  Used for unregistered selectors.
     */
    const char *name_;
    /**
     * The index of this selector in the selector table.  When a selector
     * is registered with the runtime, its name is replaced by an index
     * uniquely identifying this selector.  The index is used for dispatch.
     */
    uintptr_t index_;
  };
  /**
   * The Objective-C type encoding of the message identified by this selector.
   */
  const char * types;
};

/**
 * Returns the index of a selector.
 */
static inline uint32_t sel_index(SEL sel)
{
  assert(sel->index_ & ~(~0ull >> 1ull));
  if (sel->index_ & 1)
  {
    return sel->index_ >> 1;
  }
  else
  {
    return ((struct sel_dtable *)(sel->index_ & (~0ull >> 1ull)))->index;
  }
}

/**
 * Returns the untyped variant of a selector.
 */
__attribute__((unused))
static uint32_t get_untyped_idx(SEL aSel)
{
  SEL untyped = sel_registerTypedName_np(sel_getName(aSel), 0);
  return sel_index(untyped);
}

__attribute__((unused))
static SEL sel_getUntyped(SEL aSel)
{
  return sel_registerTypedName_np(sel_getName(aSel), 0);
}

/**
 * Returns whether a selector is mapped.
 */
BOOL isSelRegistered(SEL sel);

/**
 * Registers the selector.  This selector may be returned later, so it must not
 * be freed.
 */
SEL objc_register_selector(SEL aSel);

/**
 * SELECTOR() macro to work around the fact that GCC hard-codes the type of
 * selectors.  This is functionally equivalent to @selector(), but it ensures
 * that the selector has the type that the runtime uses for selectors.
 */
#ifdef __clang__
#define SELECTOR(x) @selector(x)
#else
#define SELECTOR(x) (SEL)@selector(x)
#endif

#endif // OBJC_SELECTOR_H_INCLUDED
