# software-project
# Moderated Learning Management System (LMS)

## Project Overview
The Moderated Learning Management System (LMS) is a C++17-based application designed to support the management of academic activities in a secondary school environment. It provides role-based access, assignment submissions, marking, moderation and controlled release of results.

The system uses a layered architecture and SQLite for data persistence. It follows the Waterfall development model, supported by the V-Model for verification and validation.

## User Roles
The system supports four user roles:

- **Student:** Views assignments and submits coursework.
- **Lecturer:** Creates assignments, reviews submissions and records marks.
- **Head of Department (HoD):** Moderates and approves marks before release.
- **Administrator:** Manages users and system administration.

## Main Features
- Role-based access control.
- Assignment creation and submission.
- Late-submission penalties and approved extensions.
- Resubmission support.
- Marking and moderation.
- Controlled release of student results.
- Audit records for mark changes.
- SQLite database persistence.

## Technologies Used
- C++17
- SQLite
- CMake
- Git and GitHub
- GoogleTest / CTest for testing

## Project Structure
The project is organised into source code, header files, database resources, tests and documentation.

- `src/` – Implementation files.
- `include/` – Header files.
- `tests/` – Test files.
- `docs/` – Project documentation and diagrams.
- `CMakeLists.txt` – Build configuration.

*Folder names may differ depending on the final repository structure.*

## Business Rules
- Late submissions receive a penalty of 10% of the maximum mark per day, up to three days.
- Approved extensions change the applicable submission deadline.
- The latest valid submission is considered for marking.
- Marks remain unavailable to students until approved and released by the HoD.
- Every mark change must record who made the change, when it occurred, the previous mark, the new mark and the reason.

## Building the Project
Ensure that a C++17 compiler, CMake and SQLite dependencies are installed.

From the project root, run:

```bash
cmake -S . -B build
cmake --build build
```

To run the available tests:

```bash
ctest --test-dir build --output-on-failure
```

## Development Approach
The project follows the Waterfall model with V-Model verification and validation. Requirements, design, implementation, testing, configuration management, deployment and maintenance are documented as part of the software engineering project.

## Repository
This repository contains the source code, tests and supporting documentation for the Moderated Learning Management System developed for the CS4433 Software Engineering project.
