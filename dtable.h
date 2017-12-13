#include "lock.h"
#include "class.h"
#include "sarray2.h"
#include "objc/slot.h"
#include "visibility.h"
#include "selector.h"
#include <stdint.h>
#include <stdio.h>



/**
 * Checks whether the class supports ARC.
 *
 * This can be used before the dtable is installed.
 */
void checkARCAccessors(Class cls);

/**
 * Returns or creates a dispatch table.
 */
struct sel_dtable *dtable_get(SEL sel);

/**
 * Finds an implementation in the dtable.
 */
struct objc_slot *dtable_lookup(struct sel_dtable *dtable, Class class);

/**
 * Adds a method to a dtable.
 */
void dtable_insert(struct sel_dtable *dtable, Class class, Method method, BOOL replace);

/**
 * Updates the implementation of a method.
 */
void update_method_for_class(Class class, Method method);

/**
 * Sends the initialize message to a class.
 */
void objc_send_initialize(id object);

/**
 * Checks if a class was initialised.
 */
BOOL is_initialised(Class class);

/**
 * Adds methods to a class.
 */
void add_method_list_to_class(Class cls, struct objc_method_list *methods);

/**
 * Removes the hidden class.
 */
void remove_class(Class class);
