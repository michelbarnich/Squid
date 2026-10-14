//
//  swizzle.c
//  Squid
//

#include "squid.h"

#include <objc/runtime.h>

static bool hook_method(Class target_class,
                        SEL selector,
                        IMP replacement,
                        IMP *original_implementation)
{
    if (target_class == Nil || selector == NULL || replacement == NULL) {
        return false;
    }

    Method method = class_getInstanceMethod(target_class, selector);
    if (method == NULL) {
        return false;
    }

    IMP original = method_getImplementation(method);
    const char *type_encoding = method_getTypeEncoding(method);
    if (original == NULL || type_encoding == NULL) {
        return false;
    }

    // class_getInstanceMethod also searches superclasses. Adding first keeps an
    // inherited method scoped to target_class; if it already exists locally,
    // replace that class's implementation.
    if (!class_addMethod(target_class, selector, replacement, type_encoding)) {
        original = method_setImplementation(method, replacement);
        if (original == NULL) {
            return false;
        }
    }

    if (original_implementation != NULL) {
        *original_implementation = original;
    }
    return true;
}

bool squid_hook_instance_method(Class target_class,
                                SEL selector,
                                IMP replacement,
                                IMP *original_implementation)
{
    return hook_method(target_class,
                       selector,
                       replacement,
                       original_implementation);
}

bool squid_hook_class_method(Class target_class,
                             SEL selector,
                             IMP replacement,
                             IMP *original_implementation)
{
    if (target_class == Nil) {
        return false;
    }

    return hook_method(object_getClass((id)target_class),
                       selector,
                       replacement,
                       original_implementation);
}

bool squid_hook_instance_method_named(const char *class_name,
                                      const char *selector_name,
                                      IMP replacement,
                                      IMP *original_implementation)
{
    if (class_name == NULL || selector_name == NULL) {
        return false;
    }

    return squid_hook_instance_method(objc_getClass(class_name),
                                      sel_registerName(selector_name),
                                      replacement,
                                      original_implementation);
}

bool squid_hook_class_method_named(const char *class_name,
                                   const char *selector_name,
                                   IMP replacement,
                                   IMP *original_implementation)
{
    if (class_name == NULL || selector_name == NULL) {
        return false;
    }

    return squid_hook_class_method(objc_getClass(class_name),
                                   sel_registerName(selector_name),
                                   replacement,
                                   original_implementation);
}
