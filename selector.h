#ifndef OBJC_SELECTOR_H_INCLUDED
#define OBJC_SELECTOR_H_INCLUDED

#include <assert.h>
#include <stdlib.h>

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
 * Selector dispatch table.
 */
struct sel_dtable
{
  uint32_t size;
  uint32_t capacity;
  uint32_t index;
  struct objc_slot **slots;
};

/**
 * Additional information attached to a selector.
 */
struct sel_meta
{
  struct sel_dtable *dtable;
  struct sel_type_list type_list;
};

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
 * Returns the meta object for a selector.
 */
struct sel_meta *sel_meta(SEL sel);

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
