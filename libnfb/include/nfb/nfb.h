/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb public header file - NFB module
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef LIBNFB_H
#define LIBNFB_H

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

#include <nfb/fdt.h>

#ifdef __cplusplus
extern "C" {
#endif


/* ~~~~[ DEFINES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

#define NFB_PATH_DEV(n)		("/dev/nfb" __STRING(n))
#define NFB_DEFAULT_DEV_PATH    "/dev/nfb0"

/* ~~~~[ TYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/*!
 * \brief struct nfb_device  Opaque NFB device datatype
 */
struct nfb_device;

/*!
 * \brief struct nfb_comp  Opaque NFB component datatype
 */
struct nfb_comp;


/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* ~~~~[ DEVICE ] ~~~~ */

/*!
 * \brief   Get default device path, when user doesn't specify one.
 * \return
 *     - path
 *
  */
const char * nfb_default_dev_path(void);

/*!
 * \brief   Open the NFB device
 * \param[in]   path  Path to the NFB device file
 * \return
 *     - NFB device handle on success
 *     - NULL on error (errno is set)
 *
 * This is the initialization function, which must be called before other
 * library functions. Upon successful completion, the returned value is a NFB
 * device handle to be passed to other functions.
 */
struct nfb_device *nfb_open(const char *path);
struct nfb_device *nfb_open_ext(const char *devname, int oflags);

/*!
 * \brief   Close the NFB device
 * \param[in]  dev  NFB device handle
 *
 * Call this function to finish the work with NFB device and cleanup. The handle
 * passed to this function can not be used further and you must ensure to close
 * that all other opened subhandles
 * (e.g. by \ref nfb_comp_open) are closed.
 */
void nfb_close(struct nfb_device *dev);

/*!
 * \brief Retrieve NFB device Device Tree description
 * \param[in] dev  NFB device handle
 * \return
 *   - Device Tree (in FDT format) on success
 *   - NULL on error
 */
const void *nfb_get_fdt(const struct nfb_device *dev);

/*!
 * \brief Retrieve NFB device ID (index in system)
 * \param[in] dev  NFB device handle
 * \return
 *   - Non-negative ID of NFB
 *   - Negative error code on error
 */
int nfb_get_system_id(const struct nfb_device *dev);

/*!
 * \brief Return count of components present in firmware
 * \param[in] dev         NFB device handle
 * \param[in] compatible  Component 'compatible' string
 * \return
 *   - Non-negative number of components on success
 *   - Negative error code on error
 *
 * This function goes through FDT and counts all nodes with matching 'compatible' property.
 */
int nfb_comp_count(const struct nfb_device *dev, const char *compatible);

/*!
 * \brief Return FDT offset of a specific component
 * \param[in] dev         NFB device handle
 * \param[in] compatible  Component 'compatible' string
 * \param[in] index       Component index
 * \return
 *   - Non-negative FDT offset of the component on success
 *   - Negative error code on error
 *
 * This function goes through FDT and finds N-th node with matching 'compatible' property.
 */
int nfb_comp_find(const struct nfb_device *dev, const char *compatible, unsigned index);

/*!
 * \brief Return FDT offset of a specific component within specific parent component
 * \param[in] dev             NFB device handle
 * \param[in] compatible      Component 'compatible' string
 * \param[in] index           Component index
 * \param[in] parent_offset   FDT offset of the parent component
 * \return
 *   - Non-negative FDT offset of the component on success
 *   - Negative error code on error
 *
 * This function goes through FDT and finds N-th node with matching 'compatible' property, but search only nodes in specific parent.
 */
int nfb_comp_find_in_parent(const struct nfb_device *dev, const char *compatible, unsigned index, int parent_offset);

/* ~~~~[ COMPONENTS ]~~~~ */

/*!
 * \brief Get component from the user data pointer
 * \param[in] ptr         User data pointer
 * \return
 *    Component handle
 *
 * This function doesn't check validity of input pointer.
 */
struct nfb_comp *nfb_user_to_comp(void *ptr);

/*!
 * \brief Get pointer to the user data space allocated in \ref nfb_comp_open_ext
 * \param[in] comp        Component handle
 * \return
 *    Pointer to user data space
 */
void *nfb_comp_to_user(struct nfb_comp *comp);

/*!
 * \brief Open component specified by \p offset
 * \param[in] dev         NFB device handle
 * \param[in] fdt_offset  FDT offset of the component
 * \return
 *   - Component handle on success
 *   - NULL on error (errno is set)
 */
struct nfb_comp *nfb_comp_open(const struct nfb_device *dev, int fdt_offset);

/*!
 * \brief Open component specified by \p offset
 * \param[in] dev         NFB device handle
 * \param[in] fdt_offset  FDT offset of the component
 * \param[in] user_size   Size of user space allocated in component
 * \return
 *   - Component handle on success
 *   - NULL on error (errno is set)
 */
struct nfb_comp *nfb_comp_open_ext(const struct nfb_device *dev, int fdt_offset, int user_size);

/*!
 * \brief Close component
 * \param[in] component  Component handle
 *
 * Call this function when your work with the component is finished. After it
 * returns, the component handle must not be used again.
 */
void nfb_comp_close(struct nfb_comp *component);

/*!
 * \brief Get component DT path
 * \param[in] component Component handle
 *
 * Return path of component in Device Tree.
 */
const char *nfb_comp_path(struct nfb_comp *component);

/*!
 * \brief Get pointer to nfb_device structure
 * \param[in] component Component handle
 *
 * Return pointer to struct nfb_device
 */
const struct nfb_device *nfb_comp_get_device(struct nfb_comp *component);

/*!
 * \brief Lock a component feature, prevent access in other process
 * \param[in] component  Component handle
 * \param[in] features   Bitmask of user-defined features to lock
 * \return
 *   - 1 on successful lock
 *   - 0 on unsuccessful lock
 *
 * When a feature of the component is locked, no other lock() to the same
 * feature of the component shall succeed before the feature of the component
 * is unlocked again. To make this work across all processes, locking
 * is done by the driver.
 *
 * Simply put, this works as a simple mutex.
 *
 * The expected usage is:
 * ~~~~~~~~~~~~~~~~~~~~~~~
 * if (nfb_comp_lock(component, MY_F_DELETE | MY_F_ADD)) {
 *   // safe to assume no-one else has locked one of these component features
 *   ...
 *   nfb_comp_unlock(component, MY_F_DELETE | MY_F_ADD);
 * }
 * ~~~~~~~~~~~~~~~~~~~~~~~
 */
int nfb_comp_lock(const struct nfb_comp *component, uint32_t features);

/*!
 * \brief Unlock a component feature
 * \param[in] component  Component handle
 * \param[in] features   Bitmask of user-defined features to unlock
 *
 * \see \ref nfb_comp_lock
 */
void nfb_comp_unlock(const struct nfb_comp *component, uint32_t features);

/*!
 * \brief Read data from specific offset in the component
 * \param[in] comp       Component handle
 * \param[in] buf        Buffer to store the data to
 * \param[in] nbyte      Number of bytes to read
 * \param[in] offset     Offset in the component
 * \return
 *   - The amount of bytes successfully read on success
 *   - Negative error code on error (errno is set)
 *
 * \note See `man 2 pread' on Linux
 */
ssize_t nfb_comp_read(const struct nfb_comp *comp, void *buf, size_t nbyte, off_t offset);

/*!
 * \brief Write data to a specific offset in the component
 * \param[in] comp       Component handle
 * \param[in] buf        Buffer containing the data to be written
 * \param[in] nbyte      Number of bytes to write
 * \param[in] offset     Offset in the component
 * \return
 *   - The amount of bytes successfully written on success
 *   - Negative error code on error (errno is set)
 */
ssize_t nfb_comp_write(const struct nfb_comp *comp, const void *buf, size_t nbyte, off_t offset);

#define __nfb_comp_write(bits) \
static inline void nfb_comp_write##bits(struct nfb_comp *comp, off_t offset, uint##bits##_t val) \
{ \
	nfb_comp_write(comp, &val, sizeof(val), offset); \
}
#define __nfb_comp_read(bits) \
static inline uint##bits##_t nfb_comp_read##bits(struct nfb_comp *comp, off_t offset) \
{ \
	uint##bits##_t val = 0; \
	nfb_comp_read(comp, &val, sizeof(val), offset); \
	return val; \
}

__nfb_comp_write(8)
__nfb_comp_write(16)
__nfb_comp_write(32)
__nfb_comp_write(64)
__nfb_comp_read(8)
__nfb_comp_read(16)
__nfb_comp_read(32)
__nfb_comp_read(64)


//int nfb_comp_get_version(const struct nfb_comp *component);
//int nfb_comp_get_index(const struct nfb_comp *component);

void *nfb_nalloc(int numa_node, size_t size);
void nfb_nfree(int numa_node, void *ptr, size_t size);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* LIBNFB_H */
