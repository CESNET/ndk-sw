#include <nfb/nfb.h>

#define SUPERCORE_REG_CMD           0x00
#define SUPERCORE_REG_CMD_ADD       (1 << 0)
#define SUPERCORE_REG_CMD_MULT      (1 << 1)

#define SUPERCORE_REG_STATUS        0x04
#define SUPERCORE_REG_DATA          0x08

int main(int argc, char *argv[])
{
    int node;

    /* This is the path to device node, you can use
       - #define macro:   NFB_DEFAULT_DEV_PATH
       - full path:       "/dev/nfb0"
         or its shortcut: "0"
       - persistent path: "/dev/nfb/by-pci-slot/0000:03:00.0"
                          "/dev/nfb/by-serial-no/COMBO-400G1/15432"
    */
    const char *path = NFB_DEFAULT_DEV_PATH;

    struct nfb_device *dev;
    struct nfb_comp *comp;

    /* Get handle to NFB device for futher operation */
    dev = nfb_open(path);
    if (!dev)
        errx(1, "Can't open device file");

    /* Find first supercore unit in Device Tree and get its FDT node offset */
    node = nfb_comp_find(dev, "mycompany,supercore", 0);

    /* Get access to the component described with Device Tree node */
    comp = nfb_comp_open(dev, node);
    if (comp == NULL)
        errx(2, "Can't open component");

    /* Perform some writes and reads to the acceleration core */
    nfb_comp_write64(comp, SUPERCORE_REG_DATA, 0xBEEFBEEFBEEFBEEFll);
    nfb_comp_write32(comp, SUPERCORE_REG_CMD, SUPERCORE_REG_CMD_ADD);

    if (nfb_comp_read8(comp, SUPERCORE_REG_STATUS) != 0)
        errx(3, "Operation ADD failed");

    /* Cleanup */
    nfb_comp_close(comp);
    nfb_close(dev);
    return 0;
}
