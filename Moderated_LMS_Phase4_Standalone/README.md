# Moderated Learning Management System (C++17) — Phase 4

This is the updated C++17 console implementation of the Moderated LMS. It uses a layered architecture and now builds with **CMake**. SQLite is bundled with the project; OpenSSL Crypto is used for password hashing and secure random salt generation.

## 1. Technology and design choices

- **C++17** for the application.
- **SQLite 3** (bundled amalgamation) for persistent storage.
- **OpenSSL Crypto** (`RAND_bytes`, `PKCS5_PBKDF2_HMAC`, `EVP_sha256`, `CRYPTO_memcmp`) for password security.
- **CMake 3.20+** for portable configuration and builds.
- **Repository pattern**: `UserRepository`, `AcademicRepository`, `AssignmentRepository`, `SubmissionRepository`, and `MarkRepository` isolate SQL access.
- **Strategy pattern**: `IPenaltyStrategy` with `LatePenaltyStrategy` encapsulates the late-mark calculation.
- **Polymorphism**: abstract `User` base class and `Student`, `Lecturer`, `HeadOfDepartment`, and `Administrator` subclasses expose role capabilities.

### Password hashing justification

Passwords are stored as `pbkdf2_sha256$iterations$salt$hash`, never as plain text. PBKDF2-HMAC-SHA-256 is a widely implemented password-based key derivation standard supported by OpenSSL. OpenSSL supplies cryptographically secure random salts through `RAND_bytes`; each password receives a unique 16-byte salt and a 32-byte derived key using 200,000 iterations. Verification uses `CRYPTO_memcmp` for a constant-time comparison. OpenSSL is used instead of a hand-written cryptographic implementation because it is a recognised, maintained cryptographic library.

## 2. Requirements covered

- Four roles: Student, Lecturer, Head of Department (HoD), and Administrator.
- Role checks for user creation, assignment creation, student submission, lecturer marking, HoD moderation, and mark release.
- Persistent SQLite storage with prepared statements and bound parameters for application-supplied values.
- Individual extensions with a recorded reason; the latest approved extension deadline is applied.
- Resubmission history with version numbers and a single latest submission per student/assignment.
- Late penalty: 10% of the assignment maximum mark per late day, for up to three days; more than three days is rejected at submission and calculates to zero in the strategy. Approved extensions use the extended deadline.
- Mark moderation: HoD can approve or return a mark; only an approved mark can be released by the responsible HoD.
- Mark audit records include actor, timestamp, previous mark (NULL for first marking), new mark, reason, and action type.
- Exceptions and basic input validation are used to report invalid operations without terminating the interactive session.

## 3. Install the tools on Windows

The easiest supported setup for VS Code is **MSYS2 UCRT64**, which provides GCC, CMake, Ninja, and OpenSSL in one environment.

1. Install MSYS2 from <https://www.msys2.org/>.
2. Open the **MSYS2 UCRT64** terminal from the Start menu (not the plain MSYS terminal).
3. Run:

   ```bash
   pacman -Syu
   ```

   If the terminal asks you to close, close it, reopen **MSYS2 UCRT64**, and run the update command again.
4. Install the compiler and dependencies:

   ```bash
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-openssl
   ```
5. Add this folder to your Windows user `PATH`:

   ```text
   C:\msys64\ucrt64\bin
   ```

6. Close and reopen VS Code. In **Terminal → New Terminal**, check:

   ```powershell
   g++ --version
   cmake --version
   openssl version
   ```

   If any command is not recognised, check the PATH entry and restart VS Code.

## 4. Open and build in VS Code

1. Extract the ZIP file to a normal folder, for example `Documents\Moderated_LMS_Phase4_Standalone`.
2. Open VS Code.
3. Select **File → Open Folder**.
4. Select the extracted `Moderated_LMS_Phase4_Standalone` folder — the folder containing `CMakeLists.txt`.
5. Select **Terminal → New Terminal**.
6. Configure the project (first build):

   ```powershell
   cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64
   ```

7. Build it:

   ```powershell
   cmake --build build
   ```

8. Run the application:

   ```powershell
   .\build\ModeratedLMS.exe
   ```

   The first launch creates `data/lms.db` and asks you to create the first administrator account. Use a password of at least 8 characters.

### Build and run using the included scripts

After installing the tools and opening the correct project folder, you can instead run:

```bat
build.bat
```

Then:

```bat
build\ModeratedLMS.exe
```

Or run both steps using:

```bat
run.bat
```

If you configured CMake with Ninja as shown above, the included scripts will reuse the existing build directory. If CMake reports a generator mismatch, delete only the generated `build` folder and configure again using the same generator consistently.

## 5. Run the automated tests

In the VS Code terminal:

```powershell
ctest --test-dir build --output-on-failure
```

Or use:

```bat
test.bat
```

The tests cover salted password hashes, password verification, short-password rejection, late penalties, extensions, invalid date input, and SQLite user creation/authentication.

## 6. How to use the application

On the first launch, create the initial administrator. Log in using that account. The administrator menu can create users, departments, courses, and enrolments. Then log out and sign in with the relevant role:

- **Administrator**: manage users, departments, courses, and enrolments.
- **Lecturer**: view assigned courses, create assignments, grant individual extensions, view submissions, and mark submissions with feedback.
- **Head of Department**: review marks, approve or return marks with a reason, and release approved marks to students.
- **Student**: view enrolled courses and assignments, submit/resubmit files, and view released marks and feedback.

For assignment deadlines, enter dates in the exact format `YYYY-MM-DD HH:MM` (24-hour time). For submissions, provide the path to an existing file. A copy is kept under `storage/submissions`.

## 7. Project structure

```text
Moderated_LMS_Phase4_Standalone/
├── CMakeLists.txt
├── .vscode/tasks.json
├── include/lms.hpp
├── src/main.cpp
├── src/lms.cpp
├── sqlite/sqlite3.c
├── sqlite/sqlite3.h
├── tests/test_lms.cpp
├── build.bat
├── run.bat
├── test.bat
└── README.md
```

`build/`, `data/`, and `storage/` are generated at runtime/build time and are excluded from version control.

## 8. Coding standard

The code follows a simple consistent C++ style: four-space indentation; `PascalCase` for class and struct names; `camelCase` for functions and variables; descriptive names; braces for control structures; `const` references for read-only string parameters; RAII for SQLite statements and database lifetime; standard exceptions for invalid operations and database failures; and parameterised SQL statements for values supplied by users. Keep SQL access inside repository/data-access classes rather than writing SQL in the console interface.

## 9. Important academic-project limitations

This is a console prototype. It stores a submitted file path and copies the file locally; it is not a network upload service. The existing design is a coursework implementation and should receive a security and requirements review before production use. The detailed rules used here are documented above; if your approved SRS specifies different penalty thresholds or permissions, those exact SRS rules should take precedence and be adjusted in `LatePenaltyStrategy` and `LMSService`.
