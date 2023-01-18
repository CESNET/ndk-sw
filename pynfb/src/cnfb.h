#include <stdbool.h>
#include <libfdt.h>

static inline int c_fdt_totalsize(const void *fdt)
{
	return fdt_totalsize(fdt);
}
