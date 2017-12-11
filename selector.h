#ifndef OBJC_SELECTOR_H_INCLUDED
#define OBJC_SELECTOR_H_INCLUDED

#include <assert.h>
#include <stdlib.h>

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
static inline uint32_t sel_index(SEL aSel)
{
  assert(aSel->index_ & ~(~0ull >> 1ull));
  if (aSel->index_ & 1)
  {
    return aSel->index_ >> 1;
  }
  else
  {
    abort();
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
