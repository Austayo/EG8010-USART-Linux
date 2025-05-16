#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>

int open_serial(const char* portname) {
    int fd = open(portname, O_RDONLY | O_NOCTTY);
    if (fd < 0) return -1;

    struct termios tty;
    tcgetattr(fd, &tty);
    cfsetospeed(&tty, B2400);
    cfsetispeed(&tty, B2400);
    tty.c_cflag = CS8 | CREAD | CLOCAL;
    tty.c_iflag = 0;
    tty.c_oflag = 0;
    tty.c_lflag = 0;
    tcsetattr(fd, TCSANOW, &tty);

    return fd;
}

void read_eg8010_data(int fd) {
    uint8_t buffer[10];
    while (1) {
        int n = read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            // Implement parsing logic based on datasheet's packet structure
            // For example, verify header, extract data fields, and checksum
        }
    }
}

int main() {
    int fd = open_serial("/dev/ttyUSB0");
    if (fd < 0) {
        perror("Failed to open serial port");
        return 1;
    }

    read_eg8010_data(fd);
    close(fd);
    return 0;
}
