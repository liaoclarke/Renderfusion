#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <hardware/gralloc.h>
#include <xf86drm.h>
#include <radeon_drm.h>

#ifndef RADEON_INFO_TILING_CONFIG
#define RADEON_INFO_TILING_CONFIG 6
#endif

#ifndef RADEON_INFO_WANT_HYPERZ
#define RADEON_INFO_WANT_HYPERZ 7
#endif

#ifndef RADEON_INFO_WANT_CMASK
#define RADEON_INFO_WANT_CMASK 8
#endif

#ifndef RADEON_INFO_CLOCK_CRYSTAL_FREQ
#define RADEON_INFO_CLOCK_CRYSTAL_FREQ 9
#endif

#ifndef RADEON_INFO_NUM_BACKENDS
#define RADEON_INFO_NUM_BACKENDS 0xa
#endif

#ifndef RADEON_INFO_NUM_TILE_PIPES
#define RADEON_INFO_NUM_TILE_PIPES 0xb
#endif

#ifndef RADEON_INFO_BACKEND_MAP
#define RADEON_INFO_BACKEND_MAP 0xd
#endif


enum  radeon_generation {
    R300, 
    R600
};

int get_drm_gralloc_fd()
{
    const hw_module_t * mod;
    int fd, err;

    err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &mod);
    if (!err) {
        const gralloc_module_t * gr = (gralloc_module_t *)mod;
        err = -EINVAL;
        if (gr->perform)
            err = gr->perform(gr, GRALLOC_MODULE_PERFORM_GET_DRM_FD, &fd);
        if (!err && fd >= 0) return fd;
    }
    return -1;
}

int 
get_drm_value(int fd, unsigned request, const char * errname, uint32_t * out)
{
    struct drm_radeon_info info;
    int retval;
    
    memset(&info, 0, sizeof(info));

    info.value = (unsigned long)out;
    info.request = request;

    retval = drmCommandWriteRead(fd, DRM_RADEON_INFO, &info, sizeof(info));
    if (retval) {
        if (errname)
            fprintf(stderr, "radeon: Failed to get %s, error number %d\n",
                errname, retval);
        return -1;
    }
    return 0;
}


struct radeon_info {
    uint32_t pci_id;
    uint32_t gen;
    uint32_t gart_size;
    uint32_t vram_size;

    uint32_t drm_major; /* version */
    uint32_t drm_minor;
    uint32_t drm_patchlevel;

    uint32_t num_backends;
    uint32_t clock_crystal_freq;
    uint32_t tiling_config;
    uint32_t num_tile_pipes;
    uint32_t backend_map;
    int backend_map_valid;
} info;

int
collect_radeon_info(int fd)
{
    struct drm_radeon_gem_info gem_info;
    int retval;
    drmVersionPtr ver;

    ver = drmGetVersion(fd);
    printf("%s: DRM version is %d.%d.%d\n", __FUNCTION__,
        ver->version_major,
        ver->version_minor,
        ver->version_patchlevel);

    drmFreeVersion(ver);
    /* Get PCI ID. */
    if (get_drm_value(fd, RADEON_INFO_DEVICE_ID, "PCI ID",
                            &info.pci_id))
        return -1;

    /* Check PCI ID. */
    switch (info.pci_id) {
#define CHIPSET(pci_id, name, family) case pci_id:
#include "radeon/r600_pci_ids.h"
#undef CHIPSET
        info.gen = R600;
        break;

    default:
        fprintf(stderr, "radeon: Invalid PCI ID.\n");
        return -1;
    }
    printf("pci_id %4x radeon_generation is R600\n", info.pci_id);

    retval = drmCommandWriteRead(fd, DRM_RADEON_GEM_INFO, &gem_info, sizeof(gem_info));
    if (retval) {
        fprintf(stderr, "radeon: Failed to get MM info, error number %d\n",
                    retval);
        return -1;
    }

    info.gart_size = gem_info.gart_size;
    info.vram_size = gem_info.vram_size;
    printf("gart size =%4d MB\nvram size = %4d MB\n", 
            (info.gart_size>>20), 
            (info.vram_size>>20));

    retval |= get_drm_value(fd, RADEON_INFO_NUM_BACKENDS,
                        "num backends", &info.num_backends);
    retval |= get_drm_value(fd, RADEON_INFO_CLOCK_CRYSTAL_FREQ,
                        NULL, &info.clock_crystal_freq);
    retval |= get_drm_value(fd, RADEON_INFO_TILING_CONFIG, 
                        NULL, &info.tiling_config);
    if (!retval) {
        printf("nr. of backend = %4u\n", info.num_backends);
        printf("clock crystal freq = %4u\n", info.clock_crystal_freq);
        printf("tiling configuration = %x\n", info.tiling_config);
    }
    return 0;
}

int main()
{	
    int fd; 
    fd = get_drm_gralloc_fd();
    printf("drm fd = %d\n", fd);
    if (fd > 0) {
        collect_radeon_info(fd);       
    }
    return 0;
}
