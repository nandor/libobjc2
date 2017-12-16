#define __BSD_VISIBLE 1
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include "objc/runtime.h"
#include "objc/hooks.h"
#include "sarray2.h"
#include "selector.h"
#include "class.h"
#include "lock.h"
#include "method_list.h"
#include "slot_pool.h"
#include "dtable.h"
#include "visibility.h"
#include "asmconstants.h"

_Static_assert(__builtin_offsetof(struct objc_class, dtable) == DTABLE_OFFSET,
    "Incorrect dtable offset for assembly");
_Static_assert(__builtin_offsetof(SparseArray, shift) == SHIFT_OFFSET,
    "Incorrect shift offset for assembly");
_Static_assert(__builtin_offsetof(SparseArray, data) == DATA_OFFSET,
    "Incorrect data offset for assembly");
_Static_assert(__builtin_offsetof(struct objc_slot, method) == SLOT_OFFSET,
    "Incorrect slot offset for assembly");


PRIVATE mutex_t initialize_lock;

struct objc_slot *objc_get_slot(Class cls, SEL selector);
void objc_resolve_class(Class);
int objc_sync_enter(id object);
int objc_sync_exit(id object);

__attribute__((unused)) static void objc_release_object_lock(id *x)
{
  objc_sync_exit(*x);
}

/**
 * Macro that is equivalent to @synchronize, for use in C code.
 */
#define LOCK_OBJECT_FOR_SCOPE(obj) \
    __attribute__((cleanup(objc_release_object_lock)))\
    __attribute__((unused)) id lock_object_pointer = obj;\
    objc_sync_enter(obj);

uint64_t dtable_bytes = 0;

void log_dtable_memory_usage(void)
{
  fprintf(stderr, "%d slot_pool\n", slot_pool_size);
  fprintf(stderr, "%llu dtable\n", dtable_bytes);
}

/**
 * Initializes the dispatch tables.
 */
PRIVATE void init_dispatch_tables(void)
{
  LOCK(&initialize_lock);
}

/**
 * Checks if the class implements a method for the specified selector.
 */
static BOOL ownsMethod(Class cls, SEL sel)
{
  struct objc_slot *slot = objc_get_slot(cls, sel);
  if ((NULL != slot) && (slot->owner == cls))
  {
    return YES;
  }
  return NO;
}

/**
 * Checks for ARC accessors, based on the dtable.
 */
PRIVATE void checkARCAccessors(Class cls)
{
  static SEL retain, release, autorelease, isARC;
  if (NULL == retain)
  {
    retain = sel_registerName("retain");
    release = sel_registerName("release");
    autorelease = sel_registerName("autorelease");
    isARC = sel_registerName("_ARCCompliantRetainRelease");
  }
  struct objc_slot *slot = objc_get_slot(cls, retain);
  if ((NULL != slot) && !ownsMethod(slot->owner, isARC))
  {
    objc_clear_class_flag(cls, objc_class_flag_fast_arc);
    return;
  }
  slot = objc_get_slot(cls, release);
  if ((NULL != slot) && !ownsMethod(slot->owner, isARC))
  {
    objc_clear_class_flag(cls, objc_class_flag_fast_arc);
    return;
  }
  slot = objc_get_slot(cls, autorelease);
  if ((NULL != slot) && !ownsMethod(slot->owner, isARC))
  {
    objc_clear_class_flag(cls, objc_class_flag_fast_arc);
    return;
  }
  objc_set_class_flag(cls, objc_class_flag_fast_arc);
}

struct sel_dtable *dtable_get(SEL sel)
{
  if (!isSelRegistered(sel))
  {
    objc_register_selector(sel);
  }
  return (struct sel_dtable *)(sel->index_ & (~0ull >> 1ull));
}


struct objc_slot *dtable_lookup(struct sel_dtable *dtable, Class class)
{
  while (class != Nil)
  {
    uint32_t size = dtable->size;
    struct objc_slot **slots = dtable->slots;

    int beg = 0, end = size - 1;
    while (beg <= end)
    {
      int mid = beg + ((end - beg) >> 1);
      if (slots[mid]->owner == class)
      {
        return slots[mid];
      }
      else if (slots[mid]->owner < class)
      {
        beg = mid + 1;
      }
      else
      {
        end = mid - 1;
      }
    }

    class = class->super_class;
  }

  return NULL;
}

static inline void clear_cache(struct sel_dtable *dtable)
{
  spin_lock(&dtable->lock);
  dtable->next = 0;
  memset(dtable->entries, 0, sizeof(dtable->entries));
  spin_unlock(&dtable->lock);
}

void dtable_insert(struct sel_dtable *dtable, Class class, Method method, BOOL replace)
{
  struct objc_slot **slots;
  if (dtable->size + 1 >= dtable->capacity)
  {
    size_t capacity = dtable->capacity ? (dtable->capacity << 1) : 2;
    size_t bytes = sizeof(struct objc_slot *) * capacity;
    slots = (struct objc_slot **)malloc(bytes);
    __sync_fetch_and_add(&dtable_bytes, bytes);
    for (int i = 0; i < dtable->size; ++i)
    {
      slots[i] = dtable->slots[i];
    }
    for (int i = dtable->size; i < capacity; ++i)
    {
      slots[i] = NULL;
    }
    dtable->slots = slots;
    dtable->capacity = capacity;
  }
  else
  {
    slots = dtable->slots;
  }
  for (int i = 0; i < dtable->size; ++i)
  {
    if (dtable->slots[i]->owner == class)
    {
      if (replace)
      {
        dtable->slots[i]->method = method->imp;
      }
      clear_cache(dtable);
      return;
    }
  }

  struct objc_slot *slot = new_slot_for_method_in_class(method, class);
  if (dtable->size == 0)
  {
    slots[dtable->size] = slot;
    dtable->size += 1;
  }
  else
  {
    if (class > slots[dtable->size - 1]->owner)
    {
      slots[dtable->size] = slot;
      dtable->size += 1;
    }
    else
    {
      slots[dtable->size] = slots[dtable->size - 1];
      dtable->size += 1;

      int i;
      for (i = dtable->size - 2; i > 0; --i)
      {
        if (slots[i]->owner > class)
        {
          slots[i] = slots[i - 1];
        }
        else
        {
          break;
        }
      }

      slots[i] = slot;
    }
  }
}

static void update_dtable(struct sel_dtable *dtable, Class class, Method method)
{
  for (int i = 0; i < dtable->size; ++i)
  {
    if (dtable->slots[i]->owner == class)
    {
      dtable->slots[i]->method = method->imp;
      dtable->slots[i]->version += 1;
    }
  }
  clear_cache(dtable);
}

PRIVATE void update_method_for_class(Class class, Method method)
{
  LOCK_RUNTIME();
  update_dtable(dtable_get(method->selector), class, method);
  update_dtable(dtable_get(sel_getUntyped(method->selector)), class, method);
}

static void register_methods(Class class)
{
  for (struct objc_method_list *l = class->methods; l; l = l->next)
  {
    for (int i = 0; i < l->count; ++i)
    {
      struct objc_method *m = &l->methods[i];
      SEL typed = m->selector;
      SEL untyped = sel_getUntyped(typed);
      dtable_insert(dtable_get(typed), class, m, NO);
      dtable_insert(dtable_get(untyped), class, m, NO);
    }
  }
}

typedef struct init_list_
{
  Class class;
  struct init_list_ *next;
} InitList;

InitList *init_list;


static void remove_dtable(InitList* meta_buffer)
{
  LOCK(&initialize_lock);
  InitList *buffer = meta_buffer->next;


  // Install the dtables.
  meta_buffer->class->dtable = (void *)1;
  buffer->class->dtable = (void *)1;

  // Remove the look-aside buffer entry.
  if (init_list == meta_buffer)
  {
    init_list = buffer->next;
  }
  else
  {
    InitList *prev = init_list;
    while (prev->next->class != meta_buffer->class)
    {
      prev = prev->next;
    }
    prev->next = buffer->next;
  }
  UNLOCK(&initialize_lock);
}

void objc_send_initialize(id object)
{
  Class class = classForObject(object);
  // If the first message is sent to an instance (weird, but possible and
  // likely for things like NSConstantString, make sure +initialize goes to
  // the class not the metaclass.
  if (objc_test_class_flag(class, objc_class_flag_meta))
  {
    class = (Class)object;
  }
  Class meta = class->isa;

  // Make sure that the class is resolved.
  objc_resolve_class(class);

  // Make sure that the superclass is initialized first.
  if (Nil != class->super_class)
  {
    objc_send_initialize((id)class->super_class);
  }

  // Lock the runtime while we're creating dtables and before we acquire any
  // other locks.  This prevents a lock-order reversal when
  // dtable_for_class is called from something holding the runtime lock while
  // we're still holding the initialize lock.  We should ensure that we never
  // acquire the runtime lock after acquiring the initialize lock.
  LOCK_RUNTIME();

  // Superclass +initialize might possibly send a message to this class, in
  // which case this method would be called again.  See NSObject and
  // NSAutoreleasePool +initialize interaction in GNUstep.
  if (objc_test_class_flag(class, objc_class_flag_initialized))
  {
    // We know that initialization has started because the flag is set.
    // Check that it's finished by grabbing the class lock.  This will be
    // released once the class has been fully initialized. The runtime
    // lock needs to be released first to prevent a deadlock between the
    // runtime lock and the class-specific lock.
    UNLOCK_RUNTIME();

    objc_sync_enter((id)meta);
    objc_sync_exit((id)meta);
    assert(class->dtable);
    return;
  }

  LOCK_OBJECT_FOR_SCOPE((id)meta);
  LOCK(&initialize_lock);
  if (objc_test_class_flag(class, objc_class_flag_initialized))
  {
    UNLOCK(&initialize_lock);
    UNLOCK_RUNTIME();
    return;
  }
  BOOL skipMeta = objc_test_class_flag(meta, objc_class_flag_initialized);

  // Set the initialized flag on both this class and its metaclass, to make
  // sure that +initialize is only ever sent once.
  objc_set_class_flag(class, objc_class_flag_initialized);
  objc_set_class_flag(meta, objc_class_flag_initialized);

  register_methods(class);
  if (!skipMeta)
  {
    register_methods(meta);
  }

  // Now we've finished doing things that may acquire the runtime lock, so we
  // can hold onto the initialise lock to make anything doing
  // dtable_for_class block until we've finished updating temporary dtable
  // lists.
  // If another thread holds the runtime lock, it can now proceed until it
  // gets into a dtable_for_class call, and then block there waiting for us
  // to finish setting up the temporary dtable.
  UNLOCK_RUNTIME();

  static SEL initializeSel = 0;
  if (0 == initializeSel)
  {
    initializeSel = sel_registerName("initialize");
  }

  struct objc_slot *initializeSlot = skipMeta ? 0 : objc_get_slot(meta, initializeSel);

  // If there's no initialize method, then don't bother installing and
  // removing the initialize dtable, just install both dtables correctly now
  if (0 == initializeSlot)
  {
    if (!skipMeta)
    {
      meta->dtable = (void *)1;
    }
    class->dtable = (void *)1;
    checkARCAccessors(class);
    UNLOCK(&initialize_lock);
    return;
  }

  // Create an entry in the dtable look-aside buffer for this.  When sending
  // a message to this class in future, the lookup function will check this
  // buffer if the receiver's dtable is not installed, and block if
  // attempting to send a message to this class.
  InitList buffer = { class, init_list };
  __attribute__((cleanup(remove_dtable)))
  InitList meta_buffer = { meta, &buffer };
  init_list = &meta_buffer;

  // We now release the initialize lock.  We'll reacquire it later when we do
  // the cleanup, but at this point we allow other threads to get the
  // temporary dtable and call +initialize in other threads.
  UNLOCK(&initialize_lock);
  // We still hold the class lock at this point.  dtable_for_class will block
  // there after acquiring the temporary dtable.

  checkARCAccessors(class);

  // Store the buffer in the temporary dtables list.  Note that it is safe to
  // insert it into a global list, even though it's a temporary variable,
  // because we will clean it up after this function.
  initializeSlot->method((id)class, initializeSel);
}

BOOL is_initialised(Class class)
{
  if (class->dtable)
  {
    return YES;
  }

  if (!objc_test_class_flag(class, objc_class_flag_initialized))
  {
    return NO;
  }

  BOOL wait;
  {
    LOCK_FOR_SCOPE(&initialize_lock);
    InitList *list = init_list;
    while (list)
    {
      if (list->class == class)
      {
        wait = YES;
        break;
      }
      list = list->next;
    }
  }

  if (wait)
  {
    objc_sync_enter((id)class);
    objc_sync_exit((id)class);
  }
  return YES;
}

PRIVATE void add_method_list_to_class(Class cls, struct objc_method_list *methods)
{
  for (int i = 0; i < methods->count; ++i)
  {
    struct objc_method *m = &methods->methods[i];
    SEL typed = m->selector;
    SEL untyped = sel_getUntyped(typed);
    dtable_insert(dtable_get(typed), cls, m, YES);
    dtable_insert(dtable_get(untyped), cls, m, YES);
  }
}

static void remove_method(struct sel_dtable *dtable, Class class)
{
  for (int i = 0; i < dtable->size; ++i)
  {
    if (dtable->slots[i]->owner == class)
    {
      for (int j = i + 1; j < dtable->size; ++j)
      {
        dtable->slots[j - 1] = dtable->slots[j];
      }
      dtable->size -= 1;
      i -= 1;
    }
  }
  clear_cache(dtable);
}

static void clear_caches(Class class)
{
  for (struct objc_method_list *l = class->methods; l; l = l->next)
  {
    for (int i = 0; i < l->count; ++i)
    {
      struct objc_method *m = &l->methods[i];
      clear_cache(dtable_get(m->selector));
      clear_cache(dtable_get(sel_getUntyped(m->selector)));
    }
  }
  if (class->super_class)
  {
    clear_caches(class->super_class);
  }
}

void remove_class(Class class)
{
  for (struct objc_method_list *l = class->methods; l; l = l->next)
  {
    for (int i = 0; i < l->count; ++i)
    {
      struct objc_method *m = &l->methods[i];
      SEL typed = m->selector;
      SEL untyped = sel_getUntyped(typed);
      remove_method(dtable_get(typed), class);
      remove_method(dtable_get(untyped), class);
    }
  }
  clear_caches(class);
}
