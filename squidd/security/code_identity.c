//
//  code_identity.c
//  Squid
//

#include "code_identity.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

bool copy_process_signing_identifier(
    pid_t pid,
    char *buffer,
    size_t buffer_size
)
{
    CFNumberRef pid_number = CFNumberCreate(
        kCFAllocatorDefault,
        kCFNumberIntType,
        &pid
    );
    if (pid_number == NULL) {
        return false;
    }

    const void *keys[] = { kSecGuestAttributePid };
    const void *values[] = { pid_number };
    CFDictionaryRef attributes = CFDictionaryCreate(
        kCFAllocatorDefault,
        keys,
        values,
        1,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );
    CFRelease(pid_number);

    if (attributes == NULL) {
        return false;
    }

    SecCodeRef code = NULL;
    OSStatus guest_status = SecCodeCopyGuestWithAttributes(
        NULL,
        attributes,
        kSecCSDefaultFlags,
        &code
    );
    CFRelease(attributes);

    if (guest_status != errSecSuccess || code == NULL) {
        return false;
    }

    CFDictionaryRef signing_information = NULL;
    OSStatus information_status = SecCodeCopySigningInformation(
        code,
        kSecCSDefaultFlags,
        &signing_information
    );
    CFRelease(code);

    if (information_status != errSecSuccess || signing_information == NULL) {
        return false;
    }

    CFStringRef identifier = CFDictionaryGetValue(
        signing_information,
        kSecCodeInfoIdentifier
    );
    bool copied = identifier != NULL && CFStringGetCString(
        identifier,
        buffer,
        (CFIndex)buffer_size,
        kCFStringEncodingUTF8
    );

    CFRelease(signing_information);
    return copied;
}
