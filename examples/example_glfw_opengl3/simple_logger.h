#pragma once
#include <iostream>
#include <string>
#include <functional>

// Simple logger for GUI implementation
class SimpleLogger {
public:
    using LogCallback = std::function<void(const std::string&)>;
    
    static void Log(const std::string& message) {
        if (callback_) {
            callback_(message);
        } else {
            std::cout << "[ESS] " << message << std::endl;
        }
    }
    
    static void SetCallback(LogCallback cb) {
        callback_ = cb;
    }
    
    static void ClearCallback() {
        callback_ = nullptr;
    }
    
private:
    static inline LogCallback callback_ = nullptr;
};