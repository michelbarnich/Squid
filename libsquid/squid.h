//
//  squid.h
//  Squid
//

#ifndef SQUID_H
#define SQUID_H

#include <objc/objc.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define SQUID_EXPORT __attribute__((visibility("default")))

/**
 Replaces an Objective-C instance method.

 The previous implementation is written to original_implementation when that
 pointer is non-null. If the method is inherited, an override is added to the
 requested class instead of modifying its superclass.
 */
SQUID_EXPORT bool squid_hook_instance_method(
    Class target_class,
    SEL selector,
    IMP replacement,
    IMP *original_implementation
);

/**
 Replaces an Objective-C class method.

 The previous implementation is written to original_implementation when that
 pointer is non-null.
 */
SQUID_EXPORT bool squid_hook_class_method(
    Class target_class,
    SEL selector,
    IMP replacement,
    IMP *original_implementation
);

/**
 Convenience variants for tweaks that resolve classes and selectors by name.
 */
SQUID_EXPORT bool squid_hook_instance_method_named(
    const char *class_name,
    const char *selector_name,
    IMP replacement,
    IMP *original_implementation
);

SQUID_EXPORT bool squid_hook_class_method_named(
    const char *class_name,
    const char *selector_name,
    IMP replacement,
    IMP *original_implementation
);

#if defined(__cplusplus)
}
#endif

#endif // SQUID_H
