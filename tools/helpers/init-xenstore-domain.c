#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <xenctrl.h>
#include <xc_dom.h>
#include <xenstore.h>
#include <xen/sys/xenbus_dev.h>
#include <xen-xsm/flask/flask.h>

static uint32_t domid = ~0;

static int build(xc_interface *xch, int argc, char** argv)
{
    char cmdline[512];
    uint32_t ssid;
    xen_domain_handle_t handle = { 0 };
    int rv, xs_fd;
    struct xc_dom_image *dom = NULL;
    int maxmem = atoi(argv[2]);
    int limit_kb = (maxmem + 1) * 1024;

    xs_fd = open("/dev/xen/xenbus_backend", O_RDWR);
    if ( xs_fd == -1 )
    {
        fprintf(stderr, "Could not open /dev/xen/xenbus_backend\n");
        return -1;
    }

    rv = xc_flask_context_to_sid(xch, argv[3], strlen(argv[3]), &ssid);
    if ( rv )
    {
        fprintf(stderr, "xc_flask_context_to_sid failed\n");
        goto err;
    }
    rv = xc_domain_create(xch, ssid, handle, 0, &domid, NULL);
    if ( rv )
    {
        fprintf(stderr, "xc_domain_create failed\n");
        goto err;
    }
    rv = xc_domain_max_vcpus(xch, domid, 1);
    if ( rv )
    {
        fprintf(stderr, "xc_domain_max_vcpus failed\n");
        goto err;
    }
    rv = xc_domain_setmaxmem(xch, domid, limit_kb);
    if ( rv )
    {
        fprintf(stderr, "xc_domain_setmaxmem failed\n");
        goto err;
    }
    rv = xc_domain_set_memmap_limit(xch, domid, limit_kb);
    if ( rv )
    {
        fprintf(stderr, "xc_domain_set_memmap_limit failed\n");
        goto err;
    }

    rv = ioctl(xs_fd, IOCTL_XENBUS_BACKEND_SETUP, domid);
    if ( rv < 0 )
    {
        fprintf(stderr, "Xenbus setup ioctl failed\n");
        goto err;
    }
    snprintf(cmdline, 512, "--event %d --internal-db", rv);

    dom = xc_dom_allocate(xch, cmdline, NULL);
    rv = xc_dom_kernel_file(dom, argv[1]);
    if ( rv )
    {
        fprintf(stderr, "xc_dom_kernel_file failed\n");
        goto err;
    }

    if ( argc > 4 )
    {
        rv = xc_dom_ramdisk_file(dom, argv[4]);
        if ( rv )
        {
            fprintf(stderr, "xc_dom_ramdisk_file failed\n");
            goto err;
        }
    }

    rv = xc_dom_boot_xen_init(dom, xch, domid);
    if ( rv )
    {
        fprintf(stderr, "xc_dom_boot_xen_init failed\n");
        goto err;
    }
    rv = xc_dom_parse_image(dom);
    if ( rv )
    {
        fprintf(stderr, "xc_dom_parse_image failed\n");
        goto err;
    }
    rv = xc_dom_mem_init(dom, maxmem);
    if ( rv )
    {
        fprintf(stderr, "xc_dom_mem_init failed\n");
        goto err;
    }
    rv = xc_dom_boot_mem_init(dom);
    if ( rv )
    {
        fprintf(stderr, "xc_dom_boot_mem_init failed\n");
        goto err;
    }
    rv = xc_dom_build_image(dom);
    if ( rv )
    {
        fprintf(stderr, "xc_dom_build_image failed\n");
        goto err;
    }
    rv = xc_dom_boot_image(dom);
    if ( rv )
    {
        fprintf(stderr, "xc_dom_boot_image failed\n");
        goto err;
    }

    xc_dom_release(dom);
    dom = NULL;

    rv = xc_domain_set_virq_handler(xch, domid, VIRQ_DOM_EXC);
    if ( rv )
    {
        fprintf(stderr, "xc_domain_set_virq_handler failed\n");
        goto err;
    }
    rv = xc_domain_unpause(xch, domid);
    if ( rv )
    {
        fprintf(stderr, "xc_domain_unpause failed\n");
        goto err;
    }

    return 0;

err:
    if ( dom )
        xc_dom_release(dom);
    if ( domid != ~0 )
        xc_domain_destroy(xch, domid);
    close(xs_fd);
    return rv;
}

int main(int argc, char** argv)
{
    xc_interface *xch;
    struct xs_handle *xsh;
    char buf[16];
    int rv, fd;

    if ( argc < 4 || argc > 5 )
    {
        fprintf(stderr,
                "Use: %s <xenstore-kernel> <memory_mb> <flask-label> [<ramdisk-file>]\n",
            argv[0]);
        return 2;
    }

    xch = xc_interface_open(NULL, NULL, 0);
    if ( !xch )
    {
        fprintf(stderr, "xc_interface_open() failed\n");
        return 1;
    }

    rv = build(xch, argc, argv);

    xc_interface_close(xch);

    if ( rv )
        return 1;

    xsh = xs_open(0);
    rv = snprintf(buf, 16, "%d", domid);
    xs_write(xsh, XBT_NULL, "/tool/xenstored/domid", buf, rv);
    xs_daemon_close(xsh);

    fd = creat("/var/run/xenstored.pid", 0666);
    if ( fd < 0 )
    {
        fprintf(stderr, "Creating /var/run/xenstored.pid failed\n");
        return 3;
    }
    rv = snprintf(buf, 16, "domid:%d\n", domid);
    rv = write(fd, buf, rv);
    close(fd);
    if ( rv < 0 )
    {
        fprintf(stderr, "Writing domid to /var/run/xenstored.pid failed\n");
        return 3;
    }

    return 0;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */