PRAGMA foreign_keys = ON;

-- 1. Users and login details
CREATE TABLE Users (
    user_id INTEGER PRIMARY KEY,
    username TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    role TEXT NOT NULL
        CHECK (role IN ('STUDENT', 'LECTURER', 'HOD', 'ADMIN')),
    active INTEGER NOT NULL DEFAULT 1
        CHECK (active IN (0, 1))
);

-- 2. Departments
CREATE TABLE Departments (
    department_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    hod_user_id INTEGER,
    FOREIGN KEY (hod_user_id)
        REFERENCES Users(user_id)
);

-- 3. Courses
CREATE TABLE Courses (
    course_id INTEGER PRIMARY KEY,
    department_id INTEGER NOT NULL,
    course_code TEXT NOT NULL UNIQUE,
    course_name TEXT NOT NULL,
    lecturer_user_id INTEGER,
    FOREIGN KEY (department_id)
        REFERENCES Departments(department_id),
    FOREIGN KEY (lecturer_user_id)
        REFERENCES Users(user_id)
);

-- 4. Student enrolments
CREATE TABLE Enrolments (
    enrolment_id INTEGER PRIMARY KEY,
    student_user_id INTEGER NOT NULL,
    course_id INTEGER NOT NULL,
    status TEXT NOT NULL DEFAULT 'ACTIVE'
        CHECK (status IN ('ACTIVE', 'INACTIVE')),
    UNIQUE (student_user_id, course_id),
    FOREIGN KEY (student_user_id)
        REFERENCES Users(user_id),
    FOREIGN KEY (course_id)
        REFERENCES Courses(course_id)
);

-- 5. Assignments
CREATE TABLE Assignments (
    assignment_id INTEGER PRIMARY KEY,
    course_id INTEGER NOT NULL,
    title TEXT NOT NULL,
    instructions TEXT,
    deadline TEXT NOT NULL,
    max_mark REAL NOT NULL CHECK (max_mark > 0),
    FOREIGN KEY (course_id)
        REFERENCES Courses(course_id)
);

-- 6. Individual extensions
CREATE TABLE Extensions (
    extension_id INTEGER PRIMARY KEY,
    assignment_id INTEGER NOT NULL,
    student_user_id INTEGER NOT NULL,
    granted_by INTEGER NOT NULL,
    new_deadline TEXT NOT NULL,
    reason TEXT NOT NULL,
    granted_at TEXT NOT NULL,
    FOREIGN KEY (assignment_id)
        REFERENCES Assignments(assignment_id),
    FOREIGN KEY (student_user_id)
        REFERENCES Users(user_id),
    FOREIGN KEY (granted_by)
        REFERENCES Users(user_id)
);

-- 7. Submission versions
CREATE TABLE Submissions (
    submission_id INTEGER PRIMARY KEY,
    assignment_id INTEGER NOT NULL,
    student_user_id INTEGER NOT NULL,
    submitted_at TEXT NOT NULL,
    status TEXT NOT NULL
        CHECK (status IN (
            'ON_TIME',
            'LATE',
            'BEYOND_ALLOWED_PERIOD'
        )),
    version_no INTEGER NOT NULL CHECK (version_no > 0),
    file_path TEXT NOT NULL,
    is_latest INTEGER NOT NULL DEFAULT 1
        CHECK (is_latest IN (0, 1)),
    UNIQUE (assignment_id, student_user_id, version_no),
    FOREIGN KEY (assignment_id)
        REFERENCES Assignments(assignment_id),
    FOREIGN KEY (student_user_id)
        REFERENCES Users(user_id)
);

-- Only one version may be marked as the latest
-- for a particular student and assignment.
CREATE UNIQUE INDEX one_latest_submission
ON Submissions(assignment_id, student_user_id)
WHERE is_latest = 1;

-- 8. Marks and feedback
CREATE TABLE Marks (
    mark_id INTEGER PRIMARY KEY,
    submission_id INTEGER NOT NULL UNIQUE,
    lecturer_user_id INTEGER NOT NULL,
    raw_mark REAL NOT NULL CHECK (raw_mark >= 0),
    final_mark REAL NOT NULL CHECK (final_mark >= 0),
    feedback TEXT,
    moderation_status TEXT NOT NULL DEFAULT 'PENDING'
        CHECK (moderation_status IN (
            'PENDING', 'RETURNED', 'APPROVED', 'RELEASED'
        )),
    released_at TEXT,
    FOREIGN KEY (submission_id)
        REFERENCES Submissions(submission_id),
    FOREIGN KEY (lecturer_user_id)
        REFERENCES Users(user_id)
);

-- 9. Mark changes and moderation audit records
CREATE TABLE Mark_Audit (
    audit_id INTEGER PRIMARY KEY,
    mark_id INTEGER NOT NULL,
    changed_by INTEGER NOT NULL,
    changed_at TEXT NOT NULL,
    old_mark REAL,
    new_mark REAL,
    reason TEXT NOT NULL,
    action_type TEXT NOT NULL,
    FOREIGN KEY (mark_id)
        REFERENCES Marks(mark_id),
    FOREIGN KEY (changed_by)
        REFERENCES Users(user_id)
);
