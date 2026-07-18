
//path of project M5stick-C plus2
cd ~/Documents/PlatformIO/Projects/m5stick_test

code ~/Documents/PlatformIO/Projects/m5stick_test


//run test read from IMU+ENV PRO from M5STICK that only in vers15
g++ -std=c++17 -O2 \
  -I/home/ronen/Desktop/RaspBotV2_NJOrin_ver15_m5 \
  -I/home/ronen/Desktop/RaspBotV2_NJOrin_ver15_m5/external/json \
  /home/ronen/Desktop/RaspBotV2_NJOrin_ver15_m5/m5stick_comm/M5StickSerial.cpp \
  /home/ronen/Desktop/RaspBotV2_NJOrin_ver15_m5/m5stick_comm/test_m5stick_serial.cpp \
  -o /home/ronen/Desktop/RaspBotV2_NJOrin_ver15_m5/m5stick_comm/test_m5stick_serial \
  -pthread

~/Desktop/RaspBotV2_NJOrin_ver15_m5/m5stick_comm/test_m5stick_serial
