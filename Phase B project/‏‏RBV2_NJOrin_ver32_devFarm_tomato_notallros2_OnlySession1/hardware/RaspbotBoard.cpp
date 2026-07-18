#include "hardware/RaspbotBoard.h"


#include <cstdio>
#include <cerrno>


#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>


namespace
{
bool isRecoverableI2cError(int err)
{
    return err == EIO || err == EREMOTEIO || err == ENXIO || err == EBUSY;
}
}


uint8_t RaspbotBoard::clampU8(int v, int lo, int hi) {
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return static_cast<uint8_t>(v);
}


RaspbotBoard::RaspbotBoard(const std::string& i2cDevice, uint8_t i2cAddr)
    : i2cDevice_(i2cDevice), i2cAddr_(i2cAddr), fd_(-1) {}


RaspbotBoard::~RaspbotBoard() {
    closeBus();
}


bool RaspbotBoard::openBus() {
    std::lock_guard<std::recursive_mutex> lock(busMutex_);


    closeBus();


    fd_ = ::open(i2cDevice_.c_str(), O_RDWR);
    if (fd_ < 0) {
        perror("[RaspbotBoard] open");
        return false;
    }


    if (::ioctl(fd_, I2C_SLAVE, i2cAddr_) < 0) {
        perror("[RaspbotBoard] ioctl(I2C_SLAVE)");
        ::close(fd_);
        fd_ = -1;
        return false;
    }


    return true;
}


void RaspbotBoard::closeBus() {
    std::lock_guard<std::recursive_mutex> lock(busMutex_);


    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}


bool RaspbotBoard::writeBytes(const uint8_t* data, std::size_t len) {
    std::lock_guard<std::recursive_mutex> lock(busMutex_);


    if (data == nullptr || len == 0) {
        std::fprintf(stderr, "[RaspbotBoard] writeBytes invalid state\n");
        return false;
    }


    if (fd_ < 0) {
        if (!openBus()) {
            std::fprintf(stderr, "[RaspbotBoard] writeBytes failed to open bus\n");
            return false;
        }
    }


    ssize_t w = ::write(fd_, data, len);
    if (w == static_cast<ssize_t>(len)) {
        return true;
    }


    const int err = errno;
    perror("[RaspbotBoard] write");


    if (!isRecoverableI2cError(err)) {
        return false;
    }


    std::fprintf(stderr, "[RaspbotBoard] write failed, reopening I2C bus and retrying once\n");


    if (!openBus()) {
        return false;
    }


    w = ::write(fd_, data, len);
    if (w != static_cast<ssize_t>(len)) {
        perror("[RaspbotBoard] write retry");
        return false;
    }


    return true;
}


bool RaspbotBoard::writeReg1(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = { reg, value };
    return writeBytes(buf, sizeof(buf));
}


bool RaspbotBoard::readReg1(uint8_t reg, uint8_t& value) {
    std::lock_guard<std::recursive_mutex> lock(busMutex_);


    if (fd_ < 0) {
        if (!openBus()) {
            std::fprintf(stderr, "[RaspbotBoard] readReg1 invalid fd\n");
            return false;
        }
    }


    ssize_t w = ::write(fd_, &reg, 1);
    if (w != 1) {
        const int err = errno;
        perror("[RaspbotBoard] write(reg for read)");


        if (!isRecoverableI2cError(err)) {
            return false;
        }


        std::fprintf(stderr, "[RaspbotBoard] read pre-write failed, reopening I2C bus and retrying once\n");


        if (!openBus()) {
            return false;
        }


        w = ::write(fd_, &reg, 1);
        if (w != 1) {
            perror("[RaspbotBoard] write(reg for read) retry");
            return false;
        }
    }


    ssize_t r = ::read(fd_, &value, 1);
    if (r != 1) {
        const int err = errno;
        perror("[RaspbotBoard] read");


        if (!isRecoverableI2cError(err)) {
            return false;
        }


        std::fprintf(stderr, "[RaspbotBoard] read failed, reopening I2C bus and retrying once\n");


        if (!openBus()) {
            return false;
        }


        w = ::write(fd_, &reg, 1);
        if (w != 1) {
            perror("[RaspbotBoard] write(reg for read) retry2");
            return false;
        }


        r = ::read(fd_, &value, 1);
        if (r != 1) {
            perror("[RaspbotBoard] read retry");
            return false;
        }
    }


    return true;
}


bool RaspbotBoard::setMotor(uint8_t motorNumber, MotorDirection dir, uint8_t speed) {
    if (motorNumber > 3) {
        std::fprintf(stderr, "[RaspbotBoard] motorNumber must be 0..3\n");
        return false;
    }


    uint8_t buf[4] = {
        REG_MOTOR,
        motorNumber,
        static_cast<uint8_t>(dir),
        speed
    };
    return writeBytes(buf, sizeof(buf));
}


bool RaspbotBoard::stopMotor(uint8_t motorNumber) {
    return setMotor(motorNumber, MotorDirection::Forward, 0);
}


bool RaspbotBoard::setServo(uint8_t servoNumber, uint8_t angle) {
    if (servoNumber < 1 || servoNumber > 2) {
        std::fprintf(stderr, "[RaspbotBoard] servoNumber must be 1..2\n");
        return false;
    }


    uint8_t buf[3] = {
        REG_SERVO,
        servoNumber,
        clampU8(angle, 0, 180)
    };
    return writeBytes(buf, sizeof(buf));
}


bool RaspbotBoard::setBuzzer(bool on) {
    return writeReg1(REG_BUZZER, on ? 1 : 0);
}



