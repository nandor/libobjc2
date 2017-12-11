#include "lock.h"
#include "class.h"
#include "sarray2.h"
#include "objc/slot.h"
#include "visibility.h"
#include <stdint.h>
#include <stdio.h>

typedef SparseArray* dtable_t;
# define objc_dtable_lookup SparseArrayLookup

/**
 * Pointer to the sparse array representing the pretend (uninstalled) dtable.
 */
PRIVATE extern dtable_t uninstalled_dtable;

/**
 * Returns the dtable for a given class.  If we are currently in an +initialize
 * method then this will block if called from a thread other than the one
 * running the +initialize method.
 */
dtable_t dtable_for_class(Class cls);

/**
 * Returns whether a class has an installed dtable.
 */
static inline int classHasInstalledDtable(struct objc_class *cls)
{
  return (cls->dtable != uninstalled_dtable);
}

/**
 * Returns whether a class has had a dtable created.  The dtable may be
 * installed, or stored in the look-aside buffer.
 */
static inline int classHasDtable(struct objc_class *cls)
{
  return (dtable_for_class(cls) != uninstalled_dtable);
}

/**
 * Updates the dtable for a class and its subclasses.  Must be called after
 * modifying a class's method list.
 */
void objc_update_dtable_for_class(Class);
/**
 * Adds a single method list to a class.  This is used when loading categories,
 * and is faster than completely rebuilding the dtable.
 */
void add_method_list_to_class(Class cls, struct objc_method_list *list);

/**
 * Destroys a dtable.
 */
void free_dtable(dtable_t dtable);

/**
 * Checks whether the class supports ARC.  This can be used before the dtable
 * is installed.
 */
void checkARCAccessorsSlow(Class cls);
