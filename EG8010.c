#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>

#define MODULE_NAME "eg8010"
#define TTY_DEV "/dev/ttyS1"
#define PACKET_LEN 10

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aidan Heaslip");
MODULE_DESCRIPTION("EG8010 UART Monitor with /proc Interface");
MODULE_VERSION("1.0");

struct eg8010_data {
    int output_voltage;
    int output_frequency;
    int bus_voltage;
    int temperature;
    uint8_t status_flags;
};

static struct eg8010_data g_data;
static DEFINE_SPINLOCK(data_lock);

static struct task_struct *reader_thread;

// Packet parser
static void parse_packet(uint8_t *buf)
{
    uint8_t checksum = 0;
    for (int i = 0; i < 9; i++)
        checksum += buf[i];

    if (checksum != buf[9]) {
        pr_warn(MODULE_NAME ": checksum mismatch (expected 0x%02X, got 0x%02X)\n", checksum, buf[9]);
        return;
    }

    struct eg8010_data temp;
    temp.output_voltage  = ((buf[1] << 8) | buf[2]) / 10;
    temp.output_frequency = buf[3];
    temp.bus_voltage     = ((buf[4] << 8) | buf[5]) / 10;
    temp.temperature     = buf[6];
    temp.status_flags    = buf[7];

    spin_lock(&data_lock);
    g_data = temp;
    spin_unlock(&data_lock);
}

// Kernel thread for reading UART
static int eg8010_uart_thread(void *data)
{
    struct file *tty_file;
    mm_segment_t old_fs;
    uint8_t buf[PACKET_LEN];

    old_fs = get_fs();
    set_fs(KERNEL_DS);

    tty_file = filp_open(TTY_DEV, O_RDONLY | O_NOCTTY, 0);
    if (IS_ERR(tty_file)) {
        pr_err(MODULE_NAME ": Cannot open TTY device %s\n", TTY_DEV);
        set_fs(old_fs);
        return PTR_ERR(tty_file);
    }

    pr_info(MODULE_NAME ": Started reader thread on %s\n", TTY_DEV);

    while (!kthread_should_stop()) {
        uint8_t byte;
        int ret = kernel_read(tty_file, &byte, 1, &tty_file->f_pos);
        if (ret <= 0) {
            msleep(10);
            continue;
        }

        if (byte == 0x55) {
            buf[0] = byte;
            int i = 1;
            while (i < PACKET_LEN) {
                ret = kernel_read(tty_file, &buf[i], 1, &tty_file->f_pos);
                if (ret > 0) {
                    i++;
                } else {
                    msleep(1);
                }
            }

            parse_packet(buf);
        }
    }

    filp_close(tty_file, NULL);
    set_fs(old_fs);
    return 0;
}

// /proc interface
static int eg8010_proc_show(struct seq_file *m, void *v)
{
    struct eg8010_data data;

    spin_lock(&data_lock);
    data = g_data;
    spin_unlock(&data_lock);

    seq_printf(m,
        "EG8010 Inverter Status\n"
        "----------------------\n"
        "Output Voltage:   %d V\n"
        "Output Frequency: %d Hz\n"
        "Bus Voltage:      %d V\n"
        "Temperature:      %d °C\n"
        "Status Flags:     0x%02X\n",
        data.output_voltage,
        data.output_frequency,
        data.bus_voltage,
        data.temperature,
        data.status_flags
    );

    return 0;
}

static int eg8010_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, eg8010_proc_show, NULL);
}

static const struct proc_ops eg8010_proc_fops = {
    .proc_open    = eg8010_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

// Module init/exit
static int __init eg8010_init(void)
{
    if (!proc_create("eg8010", 0, NULL, &eg8010_proc_fops)) {
        pr_err(MODULE_NAME ": Failed to create /proc/eg8010\n");
        return -ENOMEM;
    }

    reader_thread = kthread_run(eg8010_uart_thread, NULL, "eg8010_reader");
    if (IS_ERR(reader_thread)) {
        pr_err(MODULE_NAME ": Failed to start UART thread\n");
        remove_proc_entry("eg8010", NULL);
        return PTR_ERR(reader_thread);
    }

    pr_info(MODULE_NAME ": Module loaded\n");
    return 0;
}

static void __exit eg8010_exit(void)
{
    if (reader_thread)
        kthread_stop(reader_thread);

    remove_proc_entry("eg8010", NULL);
    pr_info(MODULE_NAME ": Module unloaded\n");
}

module_init(eg8010_init);
module_exit(eg8010_exit);
