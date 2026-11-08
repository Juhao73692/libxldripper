#include "internal.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/storage/IOCDMedia.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/IOStorageDeviceCharacteristics.h>
#include <stdio.h>
#include <stdlib.h>

#define OPTICAL_DRIVE_CLASS "IOCDBlockStorageDevice"

struct xld_ripper_device_list {
    size_t count;
    xld_ripper_device_info *devices;
};

static void copy_string(CFTypeRef value, char *dest, size_t capacity)
{
    if (value && CFGetTypeID(value) == CFStringGetTypeID() &&
        !CFStringGetCString((CFStringRef)value, dest, capacity, kCFStringEncodingUTF8))
        dest[0] = 0;
}

static void drive_properties(io_service_t service, xld_ripper_device_info *info)
{
    IORegistryEntryGetRegistryEntryID(service, &info->registry_id);
    CFTypeRef value = IORegistryEntrySearchCFProperty(service, kIOServicePlane,
        CFSTR(kIOPropertyDeviceCharacteristicsKey), NULL,
        kIORegistryIterateRecursively | kIORegistryIterateParents);
    if (value) {
        if (CFGetTypeID(value) == CFDictionaryGetTypeID()) {
            CFDictionaryRef properties = (CFDictionaryRef)value;
            copy_string(CFDictionaryGetValue(properties, CFSTR(kIOPropertyVendorNameKey)),
                        info->vendor, sizeof(info->vendor));
            copy_string(CFDictionaryGetValue(properties, CFSTR(kIOPropertyProductNameKey)),
                        info->product, sizeof(info->product));
            copy_string(CFDictionaryGetValue(properties, CFSTR(kIOPropertyProductRevisionLevelKey)),
                        info->firmware, sizeof(info->firmware));
        }
        CFRelease(value);
    }
}

static int append(xld_ripper_device_list *list, const xld_ripper_device_info *info)
{
    xld_ripper_device_info *devices = realloc(list->devices, (list->count + 1) * sizeof(*devices));
    if (!devices) return 0;
    list->devices = devices;
    list->devices[list->count++] = *info;
    return 1;
}

static uint64_t media_drive_id(io_service_t media)
{
    io_iterator_t parents;
    if (IORegistryEntryCreateIterator(media, kIOServicePlane,
            kIORegistryIterateRecursively | kIORegistryIterateParents, &parents) != KERN_SUCCESS)
        return 0;
    uint64_t id = 0;
    io_service_t parent;
    while ((parent = IOIteratorNext(parents))) {
        if (IOObjectConformsTo(parent, OPTICAL_DRIVE_CLASS))
            IORegistryEntryGetRegistryEntryID(parent, &id);
        IOObjectRelease(parent);
        if (id) break;
    }
    IOObjectRelease(parents);
    return id;
}

static xld_ripper_status enumerate(xld_ripper_device_list *list, const char *class_name, int media)
{
    CFMutableDictionaryRef matching = IOServiceMatching(class_name);
    if (!matching) return XLD_RIPPER_NO_MEMORY;
    io_iterator_t iterator;
    if (IOServiceGetMatchingServices(MACH_PORT_NULL, matching, &iterator) != KERN_SUCCESS)
        return XLD_RIPPER_IO;
    xld_ripper_status status = XLD_RIPPER_OK;
    io_service_t service;
    while ((service = IOIteratorNext(iterator))) {
        xld_ripper_device_info info = {0};
        if (media) {
            char name[48] = {0};
            CFTypeRef bsd = IORegistryEntryCreateCFProperty(service, CFSTR(kIOBSDNameKey), NULL, 0);
            copy_string(bsd, name, sizeof(name));
            if (bsd) CFRelease(bsd);
            if (name[0]) {
                uint64_t id = media_drive_id(service);
                size_t index = 0;
                while (index < list->count && list->devices[index].registry_id != id) ++index;
                if (id && index < list->count) {
                    snprintf(list->devices[index].media_path, sizeof(info.media_path), "/dev/%s", name);
                } else {
                    drive_properties(service, &info);
                    snprintf(info.media_path, sizeof(info.media_path), "/dev/%s", name);
                    if (!append(list, &info)) status = XLD_RIPPER_NO_MEMORY;
                }
            }
        } else {
            drive_properties(service, &info);
            if (!append(list, &info)) status = XLD_RIPPER_NO_MEMORY;
        }
        IOObjectRelease(service);
        if (status != XLD_RIPPER_OK) break;
    }
    if (status == XLD_RIPPER_OK && !IOIteratorIsValid(iterator)) status = XLD_RIPPER_IO;
    IOObjectRelease(iterator);
    return status;
}

xld_ripper_status xld_ripper_device_list_create(xld_ripper_device_list **out, xld_ripper_error *error)
{
    if (!out) return ripper_error(error, XLD_RIPPER_INVALID_ARGUMENT, 0, -1);
    *out = NULL;
    xld_ripper_device_list *list = calloc(1, sizeof(*list));
    if (!list) return ripper_error(error, XLD_RIPPER_NO_MEMORY, 0, -1);
    xld_ripper_status status = enumerate(list, OPTICAL_DRIVE_CLASS, 0);
    if (status == XLD_RIPPER_OK) status = enumerate(list, kIOCDMediaClass, 1);
    if (status == XLD_RIPPER_OK) *out = list;
    else xld_ripper_device_list_free(list);
    return ripper_error(error, status, 0, -1);
}

size_t xld_ripper_device_list_count(const xld_ripper_device_list *list)
{
    return list ? list->count : 0;
}

xld_ripper_status xld_ripper_device_list_get(const xld_ripper_device_list *list, size_t index,
                                            xld_ripper_device_info *info)
{
    if (!list || !info || index >= list->count) return XLD_RIPPER_INVALID_ARGUMENT;
    *info = list->devices[index];
    return XLD_RIPPER_OK;
}

void xld_ripper_device_list_free(xld_ripper_device_list *list)
{
    if (!list) return;
    free(list->devices);
    free(list);
}
