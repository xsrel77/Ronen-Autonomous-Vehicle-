#pragma once


#include <cstdint>
#include <cstddef>
#include <string>
#include <mutex>


class RaspbotBoard {
public:
    static constexpr uint8_t kDefaultAddr = 0x2B;


    enum class MotorDirection : uint8_t {
        Forward  = 0,
        Backward = 1
    };


    explicit RaspbotBoard(const std::string& i2cDevice = "/dev/i2c-7",
                          uint8_t i2cAddr = kDefaultAddr);
    ~RaspbotBoard();


    RaspbotBoard(const RaspbotBoard&) = delete;
    RaspbotBoard& operator=(const RaspbotBoard&) = delete;


    bool openBus();
    void closeBus();


    bool isOpen() const { return fd_ >= 0; }
    int  fd() const { return fd_; }


    bool writeBytes(const uint8_t* data, std::size_t len);
    bool writeReg1(uint8_t reg, uint8_t value);
    bool readReg1(uint8_t reg, uint8_t& value);


    bool setMotor(uint8_t motorNumber, MotorDirection dir, uint8_t speed); // 0..3
    bool stopMotor(uint8_t motorNumber);


    bool setServo(uint8_t servoNumber, uint8_t angle); // 1..2, 0..180
    bool setBuzzer(bool on);


    static uint8_t clampU8(int v, int lo, int hi);


private:
    static constexpr uint8_t REG_MOTOR  = 0x01;
    static constexpr uint8_t REG_SERVO  = 0x02;
    static constexpr uint8_t REG_BUZZER = 0x06;


    std::string i2cDevice_;
    uint8_t i2cAddr_;
    int fd_;


    std::recursive_mutex busMutex_;
};



