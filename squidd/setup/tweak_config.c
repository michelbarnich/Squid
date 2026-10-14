#include "tweak_config.h"

#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool copy_cf_string(
    CFDictionaryRef dictionary,
    CFStringRef key,
    char *buffer,
    size_t buffer_size
)
{
    CFTypeRef value = CFDictionaryGetValue(dictionary, key);
    if (value == NULL || CFGetTypeID(value) != CFStringGetTypeID()) {
        return false;
    }

    return CFStringGetCString(
        (CFStringRef)value,
        buffer,
        buffer_size,
        kCFStringEncodingUTF8
    );
}

static bool has_plist_suffix(const char *name)
{
    size_t length = strlen(name);
    return length > 6 && strcmp(name + length - 6, ".plist") == 0;
}

static bool read_tweak_file(
    const char *path,
    const char *fallback_identifier,
    tweak_config_t *tweak
)
{
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) {
        return false;
    }

    struct stat status = { 0 };
    if (fstat(descriptor, &status) != 0 || status.st_size <= 0) {
        close(descriptor);
        return false;
    }

    uint8_t *bytes = malloc((size_t)status.st_size);
    if (bytes == NULL) {
        close(descriptor);
        return false;
    }

    ssize_t bytes_read = read(descriptor, bytes, (size_t)status.st_size);
    close(descriptor);
    if (bytes_read != status.st_size) {
        free(bytes);
        return false;
    }

    CFDataRef data = CFDataCreate(
        kCFAllocatorDefault,
        bytes,
        (CFIndex)status.st_size
    );
    free(bytes);
    if (data == NULL) {
        return false;
    }

    CFErrorRef error = NULL;
    CFPropertyListRef property_list = CFPropertyListCreateWithData(
        kCFAllocatorDefault,
        data,
        kCFPropertyListImmutable,
        NULL,
        &error
    );
    CFRelease(data);
    if (error != NULL) {
        CFRelease(error);
    }
    if (property_list == NULL ||
        CFGetTypeID(property_list) != CFDictionaryGetTypeID()) {
        if (property_list != NULL) {
            CFRelease(property_list);
        }
        return false;
    }

    CFDictionaryRef dictionary = (CFDictionaryRef)property_list;
    *tweak = (tweak_config_t) { 0 };
    tweak->enabled = true;

    if (!copy_cf_string(
            dictionary,
            CFSTR("Identifier"),
            tweak->identifier,
            sizeof(tweak->identifier))) {
        strlcpy(
            tweak->identifier,
            fallback_identifier,
            sizeof(tweak->identifier)
        );
    }

    bool valid = copy_cf_string(
        dictionary,
        CFSTR("TargetBundleID"),
        tweak->target_bundle_id,
        sizeof(tweak->target_bundle_id)
    );
    valid = copy_cf_string(
        dictionary,
        CFSTR("LibraryPath"),
        tweak->library_path,
        sizeof(tweak->library_path)
    ) && valid;

    copy_cf_string(
        dictionary,
        CFSTR("OSVersion"),
        tweak->os_version,
        sizeof(tweak->os_version)
    );

    CFTypeRef enabled = CFDictionaryGetValue(dictionary, CFSTR("Enabled"));
    if (enabled != NULL && CFGetTypeID(enabled) == CFBooleanGetTypeID()) {
        tweak->enabled = CFBooleanGetValue((CFBooleanRef)enabled);
    }

    CFRelease(property_list);
    return valid && tweak->target_bundle_id[0] != '\0' &&
        tweak->library_path[0] != '\0';
}

static bool copy_config_directory(char *buffer, size_t buffer_size)
{
    const char *override = getenv("SQUID_CONFIG_DIRECTORY");
    if (override != NULL && override[0] != '\0') {
        return strlcpy(buffer, override, buffer_size) < buffer_size;
    }

    CFStringRef console_user = SCDynamicStoreCopyConsoleUser(
        NULL,
        NULL,
        NULL
    );
    if (console_user == NULL) {
        return false;
    }

    char username[256] = { 0 };
    bool copied = CFStringGetCString(
        console_user,
        username,
        sizeof(username),
        kCFStringEncodingUTF8
    );
    CFRelease(console_user);
    if (!copied) {
        return false;
    }

    struct passwd *user = getpwnam(username);
    if (user == NULL || user->pw_dir == NULL) {
        return false;
    }

    return snprintf(buffer, buffer_size, "%s/.squid", user->pw_dir) <
        (int)buffer_size;
}

bool tweak_config_store_load(tweak_config_store_t *store)
{
    *store = (tweak_config_store_t) { 0 };

    char directory_path[PATH_MAX] = { 0 };
    if (!copy_config_directory(directory_path, sizeof(directory_path))) {
        fprintf(stderr, "Could not resolve the Squid configuration directory.\n");
        return false;
    }

    DIR *directory = opendir(directory_path);
    if (directory == NULL) {
        if (errno == ENOENT) {
            fprintf(stderr, "Squid configuration directory does not exist: %s\n",
                    directory_path);
            return true;
        }
        fprintf(stderr, "Could not open Squid configuration directory %s.\n",
                directory_path);
        return false;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(directory)) != NULL) {
        if (!has_plist_suffix(entry->d_name)) {
            continue;
        }

        char path[PATH_MAX] = { 0 };
        if (snprintf(path, sizeof(path), "%s/%s", directory_path,
                     entry->d_name) >= (int)sizeof(path)) {
            continue;
        }

        tweak_config_t tweak = { 0 };
        if (!read_tweak_file(path, entry->d_name, &tweak)) {
            fprintf(stderr, "Ignoring invalid tweak plist: %s\n", path);
            continue;
        }

        tweak_config_t *larger_store = realloc(
            store->tweaks,
            (store->count + 1) * sizeof(*store->tweaks)
        );
        if (larger_store == NULL) {
            closedir(directory);
            tweak_config_store_destroy(store);
            return false;
        }

        store->tweaks = larger_store;
        store->tweaks[store->count++] = tweak;
    }

    closedir(directory);
    return true;
}

void tweak_config_store_destroy(tweak_config_store_t *store)
{
    free(store->tweaks);
    *store = (tweak_config_store_t) { 0 };
}

bool tweak_config_matches(
    const tweak_config_t *tweak,
    const char *bundle_id,
    const char *os_version
)
{
    if (!tweak->enabled || strcmp(tweak->target_bundle_id, bundle_id) != 0) {
        return false;
    }

    if (tweak->os_version[0] == '\0' ||
        strcmp(tweak->os_version, "*") == 0) {
        return true;
    }

    size_t configured_length = strlen(tweak->os_version);
    return strncmp(tweak->os_version, os_version, configured_length) == 0 &&
        (os_version[configured_length] == '\0' ||
         os_version[configured_length] == '.');
}
