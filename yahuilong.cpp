#include <windows.h>
#include <iostream>
#include <string>

class SerialPort
{
private:
    HANDLE hSerial;
    DCB dcbSerialParams;
    COMMTIMEOUTS timeouts;

public:
    SerialPort(const std::string &portName)
    {
        // 打开串口
        hSerial = CreateFile(portName.c_str(),
                             GENERIC_READ | GENERIC_WRITE,
                             0,
                             nullptr,
                             OPEN_EXISTING,
                             0,
                             nullptr);

        if (hSerial == INVALID_HANDLE_VALUE)
        {
            throw std::runtime_error("Error: Unable to open port.");
        }

        // 配置串口参数
        dcbSerialParams = {0};
        dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
        if (!GetCommState(hSerial, &dcbSerialParams))
        {
            throw std::runtime_error("Error: Unable to get port state.");
        }

        dcbSerialParams.BaudRate = CBR_9600;  // 波特率
        dcbSerialParams.ByteSize = 8;        // 数据位
        dcbSerialParams.StopBits = ONESTOPBIT;
        dcbSerialParams.Parity = NOPARITY;

        if (!SetCommState(hSerial, &dcbSerialParams))
        {
            throw std::runtime_error("Error: Unable to set port parameters.");
        }

        // 配置超时
        timeouts = {0};
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = 50;
        timeouts.ReadTotalTimeoutMultiplier = 10;
        timeouts.WriteTotalTimeoutConstant = 50;
        timeouts.WriteTotalTimeoutMultiplier = 10;

        if (!SetCommTimeouts(hSerial, &timeouts))
        {
            throw std::runtime_error("Error: Unable to set timeouts.");
        }
    }

    ~SerialPort()
    {
        CloseHandle(hSerial);
    }

    void sendData(const std::string &data)
    {
        DWORD bytesWritten;
        if (!WriteFile(hSerial, data.c_str(), data.length(), &bytesWritten, nullptr))
        {
            throw std::runtime_error("Error: Failed to send data.");
        }
    }

    std::string receiveData()
    {
        char buffer[256];
        DWORD bytesRead;
        if (!ReadFile(hSerial, buffer, sizeof(buffer), &bytesRead, nullptr))
        {
            throw std::runtime_error("Error: Failed to read data.");
        }
        return std::string(buffer, bytesRead);
    }
};

int main()
{
    try
    {
        SerialPort serial("COM3"); // 根据设备更换串口号
        serial.sendData("Hello Device");
        std::string response = serial.receiveData();
        std::cout << "Received: " << response << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}