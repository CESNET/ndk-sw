#include <nfb/nfb.h>

struct nfb_device   *dev    = NULL;
const char          *str    = NULL;
const void          *fdt    = NULL;
int                 i       = 0;
unsigned            u       = 0;
struct nfb_comp     *comp   = 0;
ssize_t             ssize   = 0;
size_t              size    = 0;
off_t               offset  = 0;
void                *buf    = NULL;

int main(void) {
	dev = nfb_open(str);
	nfb_close(dev);
	fdt = nfb_get_fdt(dev);
	i = nfb_comp_count(dev, str);
	i = nfb_comp_find(dev, str, u);
	comp = nfb_comp_open(dev, i);
	nfb_comp_close(comp);
	i = nfb_comp_lock(comp, 0);
	nfb_comp_unlock(comp, 0);
	ssize = nfb_comp_read(comp, buf, size, offset);
	ssize = nfb_comp_write(comp, buf, size, offset);
}
