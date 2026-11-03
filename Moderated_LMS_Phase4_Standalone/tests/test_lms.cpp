#include "lms.hpp"
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        const auto h1 = lms::PasswordHasher::hashPassword("CorrectHorse9");
        const auto h2 = lms::PasswordHasher::hashPassword("CorrectHorse9");
        if (h1 == h2) throw std::runtime_error("Password hashes should use different salts.");
        if (!lms::PasswordHasher::verifyPassword("CorrectHorse9", h1)) throw std::runtime_error("Correct password was rejected.");
        if (lms::PasswordHasher::verifyPassword("wrong-password", h1)) throw std::runtime_error("Incorrect password was accepted.");
        bool shortRejected = false;
        try { (void)lms::PasswordHasher::hashPassword("short"); } catch (const std::invalid_argument&) { shortRejected = true; }
        if (!shortRejected) throw std::runtime_error("Short password was not rejected.");

        lms::LatePenaltyStrategy penalty;
        if (penalty.calculate(80,100,"2026-11-02 10:00","2026-11-01 10:00",false) != 70) throw std::runtime_error("One-day penalty failed.");
        if (penalty.calculate(80,100,"2026-11-04 10:00","2026-11-01 10:00",false) != 50) throw std::runtime_error("Three-day penalty failed.");
        if (penalty.calculate(80,100,"2026-11-05 10:00","2026-11-01 10:00",false) != 0) throw std::runtime_error("Beyond-three-day penalty failed.");
        if (penalty.calculate(80,100,"2026-11-03 10:00","2026-11-01 10:00",true) != 80) throw std::runtime_error("Extension rule failed.");
        bool invalidDateRejected = false;
        try { (void)penalty.calculate(80,100,"not-a-date","2026-11-01 10:00",false); } catch (const std::invalid_argument&) { invalidDateRejected = true; }
        if (!invalidDateRejected) throw std::runtime_error("Invalid date was not rejected.");

        std::filesystem::create_directories("build");
        const char* dbPath = "build/test_lms.db";
        std::remove(dbPath);
        {
            lms::LMSService app(dbPath);
            int id = app.users.create("student1", "Test Student", "Password123", lms::Role::Student);
            if (id <= 0 || !app.users.authenticate("student1", "Password123") || app.users.authenticate("student1", "wrong"))
                throw std::runtime_error("SQLite user creation/login test failed.");
        }
        std::remove(dbPath);
        std::cout << "All LMS tests passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "TEST FAILED: " << e.what() << '\n';
        return 1;
    }
}
