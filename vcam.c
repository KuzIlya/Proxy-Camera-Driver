#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/version.h>


static struct v4l2_device v4l2_dev;
static struct video_device *vdev;
static struct file *real_camera_file = NULL;

static const char *real_device_path = "/dev/video0";

static DEFINE_MUTEX(vcam_mutex);
static int device_open_count = 0;


static int vcam_open(struct file *file) {

    pr_info("Virtual camera: attempting to open device\n");

    if (!mutex_trylock(&vcam_mutex)) {
        pr_err("Virtual camera: device is already in use\n");
        return -EBUSY;
    }

    if (!real_camera_file) {
        struct file *f = filp_open(real_device_path, O_RDWR | O_NONBLOCK, 0);
        if (IS_ERR(f)) {
            pr_err("Virtual camera: failed to open real device %s\n", real_device_path);
            mutex_unlock(&vcam_mutex);
            return PTR_ERR(f);
        }
        real_camera_file = f;
    }

    device_open_count++;
    mutex_unlock(&vcam_mutex);

    pr_info("Virtual camera: device opened successfully\n");
    return 0;
}


static int vcam_release(struct file *file) {
    pr_info("Virtual camera: closing device\n");

    mutex_lock(&vcam_mutex);

    if (real_camera_file) {
        filp_close(real_camera_file, NULL);
        real_camera_file = NULL;
    }

    if (device_open_count > 0)
        device_open_count--;

    mutex_unlock(&vcam_mutex);
    pr_info("Virtual camera: device closed\n");

    return 0;
}


static ssize_t vcam_read(struct file *file, char __user *user_buffer, size_t count, loff_t *ppos) {
    ssize_t ret;
    void *buffer;

    if (!real_camera_file) {
        pr_err("vcam_read: Real camera not opened\n");
        return -EIO;
    }

    buffer = kzalloc(count, GFP_KERNEL);
    if (!buffer)
        return -ENOMEM;

    ret = kernel_read(real_camera_file, buffer, count, &real_camera_file->f_pos);
    if (ret < 0) {
        pr_err("vcam_read: Failed to read from real camera\n");
        kfree(buffer);
        return ret;
    }

    if (copy_to_user(user_buffer, buffer, ret)) {
        pr_err("vcam_read: Failed to copy data to user space\n");
        kfree(buffer);
        return -EFAULT;
    }

    pr_info("vcam_read: Successfully captured %zd bytes\n", ret);
    kfree(buffer);
    return ret;
}


static int vcam_mmap(struct file *file, struct vm_area_struct *vma)
{
    if (!real_camera_file) {
        pr_err("vcam_mmap: Real camera device not opened\n");
        return -EIO;
    }

    struct file_operations *real_fops = real_camera_file->f_op;
    if (!real_fops->mmap) {
        pr_err("vcam_mmap: Real device does not support mmap\n");
        return -EINVAL;
    }

    int ret = real_fops->mmap(real_camera_file, vma);
    if (ret < 0) {
        pr_err("vcam_mmap: Failed to mmap real camera device: %d\n", ret);
        return ret;
    }

    pr_info("vcam_mmap: Successfully mapped memory from real device\n");
    return 0;
}


static long vcam_handle_real_camera_ioctl(unsigned int cmd, unsigned long arg) {
    if (!real_camera_file) {
        pr_err("vcam_handle_real_camera_ioctl: Real camera device is not open\n");
        return -EIO;
    }

    long ret = vfs_ioctl(real_camera_file, cmd, (void __user *)arg);
    if (ret < 0) {
        pr_err("vcam_handle_real_camera_ioctl: IOCTL %u failed with error %ld\n", cmd, ret);
        return ret;
    }

    pr_info("vcam_handle_real_camera_ioctl: IOCTL %u handled successfully\n", cmd);
    return ret;
}

static long vcam_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {

    return vcam_handle_real_camera_ioctl(cmd, arg);
}


static const struct v4l2_file_operations vcam_fops = {
    .owner = THIS_MODULE,
    .open = vcam_open,
    .release = vcam_release,
    .read = vcam_read,
    .unlocked_ioctl = vcam_ioctl,
    .mmap = vcam_mmap,
};

static int __init vcam_init(void) {
    int ret;

    pr_info("Initializing virtual camera\n");

    memset(&v4l2_dev, 0, sizeof(v4l2_dev));
    strlcpy(v4l2_dev.name, "virtual-camera", sizeof(v4l2_dev.name));

    ret = v4l2_device_register(NULL, &v4l2_dev);
    if (ret) {
        pr_err("Failed to register V4L2 device: %d\n", ret);
        return ret;
    }

    vdev = video_device_alloc();
    if (!vdev) {
        pr_err("Failed to allocate video device\n");
        return -ENOMEM;
    }

    *vdev = (struct video_device) {
        .name = "vcam",
        .v4l2_dev = &v4l2_dev,
        .fops = &vcam_fops,
        .release = video_device_release,
        .device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE,
    };

    pr_info("Registering video device\n");

    ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
    if (ret) {
        pr_err("Failed to register video device: %d\n", ret);
        return ret;
    }

    pr_info("Video device registered successfully");

    pr_info("Virtual camera initialized successfully\n");
    return 0;
}

static void __exit vcam_exit(void) {
    video_unregister_device(vdev);
    v4l2_device_unregister(&v4l2_dev);
    pr_info("Virtual camera driver exited\n");
}

module_init(vcam_init);
module_exit(vcam_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("KuzIlya <KuznetsovIlyaDM@yandex.ru>");
MODULE_DESCRIPTION("Virtual Camera Driver for Single Frame Capture");
