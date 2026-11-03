#pragma once

#include "../sqlite/sqlite3.h"
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

namespace lms {

enum class Role { Student, Lecturer, HoD, Administrator };
std::string roleToString(Role role);
Role roleFromString(const std::string& value);

struct User {
    int id{};
    std::string username;
    std::string fullName;
    Role role{Role::Student};
    bool active{true};
    virtual ~User() = default;
    virtual std::string roleName() const = 0;
    virtual bool canManageUsers() const { return false; }
    virtual bool canManageCourses() const { return false; }
    virtual bool canTeach() const { return false; }
    virtual bool canModerate() const { return false; }
};
struct Student final : User {
    std::string roleName() const override { return "Student"; }
};
struct Lecturer final : User {
    std::string roleName() const override { return "Lecturer"; }
    bool canTeach() const override { return true; }
};
struct HeadOfDepartment final : User {
    std::string roleName() const override { return "Head of Department"; }
    bool canModerate() const override { return true; }
};
struct Administrator final : User {
    std::string roleName() const override { return "Administrator"; }
    bool canManageUsers() const override { return true; }
    bool canManageCourses() const override { return true; }
};

struct Assignment {
    int id{};
    int courseId{};
    std::string title;
    std::string instructions;
    std::string deadline; // ISO 8601: YYYY-MM-DD HH:MM
    double maxMark{};
};
struct Submission {
    int id{};
    int assignmentId{};
    int studentId{};
    std::string submittedAt;
    std::string status;
    int version{};
    std::string filePath;
    bool latest{};
};
struct Mark {
    int id{};
    int submissionId{};
    int lecturerId{};
    double rawMark{};
    double finalMark{};
    std::string feedback;
    std::string moderationStatus;
};

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    sqlite3* handle() const { return db_; }
    void execute(const std::string& sql);
private:
    sqlite3* db_{nullptr};
};

class PasswordHasher {
public:
    static std::string hashPassword(const std::string& password);
    static bool verifyPassword(const std::string& password, const std::string& stored);
};

class IPenaltyStrategy {
public:
    virtual ~IPenaltyStrategy() = default;
    virtual double calculate(double rawMark, double maxMark,
                             const std::string& submittedAt,
                             const std::string& deadline,
                             bool approvedExtension) const = 0;
};
class LatePenaltyStrategy final : public IPenaltyStrategy {
public:
    double calculate(double rawMark, double maxMark,
                     const std::string& submittedAt,
                     const std::string& deadline,
                     bool approvedExtension) const override;
};

class UserRepository {
public:
    explicit UserRepository(Database& db) : db_(db) {}
    int create(const std::string& username, const std::string& fullName,
               const std::string& password, Role role);
    std::shared_ptr<User> authenticate(const std::string& username,
                                       const std::string& password);
    std::shared_ptr<User> findById(int id);
    std::vector<std::shared_ptr<User>> all();
    void setActive(int id, bool active);
private:
    Database& db_;
};
class AcademicRepository {
public:
    explicit AcademicRepository(Database& db) : db_(db) {}
    int createDepartment(const std::string& name, int hodId);
    int createCourse(int departmentId, const std::string& code,
                     const std::string& name, int lecturerId);
    void enrol(int studentId, int courseId);
    std::vector<std::string> coursesForStudent(int studentId);
    std::vector<std::string> coursesForLecturer(int lecturerId);
    bool studentEnrolled(int studentId, int courseId);
    bool lecturerAssigned(int lecturerId, int courseId);
    bool hodOwnsCourse(int hodId, int courseId);
    std::vector<std::string> departments();
private:
    Database& db_;
};
class AssignmentRepository {
public:
    explicit AssignmentRepository(Database& db) : db_(db) {}
    int create(int courseId, const std::string& title, const std::string& instructions,
               const std::string& deadline, double maxMark);
    Assignment get(int id);
    std::vector<Assignment> forCourse(int courseId);
    std::vector<Assignment> forStudent(int studentId);
    void grantExtension(int assignmentId, int studentId, int lecturerId,
                        const std::string& newDeadline, const std::string& reason);
    std::string applicableDeadline(int assignmentId, int studentId);
private:
    Database& db_;
};
class SubmissionRepository {
public:
    explicit SubmissionRepository(Database& db) : db_(db) {}
    int save(int assignmentId, int studentId, const std::string& submittedAt,
             const std::string& status, const std::string& filePath);
    std::vector<Submission> pendingForLecturer(int lecturerId);
    std::vector<Submission> latestForAssignment(int assignmentId);
    Submission get(int id);
private:
    Database& db_;
};
class MarkRepository {
public:
    explicit MarkRepository(Database& db) : db_(db) {}
    int saveOrUpdate(int submissionId, int lecturerId, double raw, double finalMark,
                     const std::string& feedback, int changedBy, const std::string& reason);
    std::vector<std::string> pendingForHoD(int hodId);
    void moderate(int markId, int hodId, bool approve, const std::string& reason);
    void release(int markId, int hodId);
    std::vector<std::string> releasedForStudent(int studentId);
private:
    Database& db_;
};

class LMSService {
private:
    Database db_;
    std::unique_ptr<IPenaltyStrategy> penalty_;
public:
    explicit LMSService(const std::string& databasePath);
    Database& database() { return db_; }
    UserRepository users;
    AcademicRepository academics;
    AssignmentRepository assignments;
    SubmissionRepository submissions;
    MarkRepository marks;
    int createUser(const User& actor, const std::string& username, const std::string& name,
                   const std::string& password, Role role);
    int createDepartment(const User& actor, const std::string& name, int hodId);
    int createCourse(const User& actor, int departmentId, const std::string& code,
                     const std::string& name, int lecturerId);
    void enrolStudent(const User& actor, int studentId, int courseId);
    void setUserActive(const User& actor, int userId, bool active);
    void grantExtension(const User& actor, int assignmentId, int studentId,
                        const std::string& newDeadline, const std::string& reason);
    int createAssignment(const User& actor, int courseId, const std::string& title,
                         const std::string& instructions, const std::string& deadline, double maxMark);
    int submit(const User& actor, int assignmentId, const std::string& submittedAt,
               const std::string& sourceFile);
    void markSubmission(const User& actor, int submissionId, double rawMark,
                        const std::string& feedback, const std::string& reason);
    void moderate(const User& actor, int markId, bool approve, const std::string& reason);
    void release(const User& actor, int markId);
};

std::string nowLocal();
std::string prompt(const std::string& label);
int promptInt(const std::string& label);
double promptDouble(const std::string& label);
void runApplication();

} // namespace lms
